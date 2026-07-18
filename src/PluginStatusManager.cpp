#include "PluginStatusManager.h"

#include "DaemonProcess.h"
#include "JsonStd.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

QString baseUrl() {
	return QString::fromLatin1("http://127.0.0.1:%1").arg(DaemonProcess::kPort);
}

PluginStatus parsePlugin(const QVariantMap &obj) {
	PluginStatus status;
	status.pluginId = obj.value("plugin_id").toString();
	status.state = obj.value("state").toString();
	const QVariant pid = obj.value("pid");
	status.pid = pid.isNull() ? -1 : pid.toLongLong();
	const QVariant memoryBytes = obj.value("memory_bytes");
	status.memoryBytes = memoryBytes.isNull() ? -1 : memoryBytes.toLongLong();
	const QVariant lastUsed = obj.value("last_used");
	status.lastUsed = lastUsed.isNull() ? 0.0 : lastUsed.toDouble();
	return status;
}

} // namespace

PluginStatusManager::PluginStatusManager(QObject *parent) : QObject(parent) {
	m_networkManager = new QNetworkAccessManager(this);
	// Old-style SIGNAL()/SLOT() connect, not PMF-based -- see the header's
	// comment on onListReplyFinished()/onActionReplyFinished().
	connect(&m_timer, SIGNAL(timeout()), this, SLOT(refresh()));
}

void PluginStatusManager::start(int intervalMs) {
	m_timer.start(intervalMs);
	refresh();
}

void PluginStatusManager::stop() {
	m_timer.stop();
}

void PluginStatusManager::refresh() {
	// Skip this tick if the previous request hasn't completed yet -- avoids
	// piling up requests if the daemon is slow to respond.
	if (m_requestInFlight) {
		return;
	}
	m_requestInFlight = true;

	QNetworkRequest request(QUrl(baseUrl() + "/plugins"));
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}
	QNetworkReply *reply = m_networkManager->get(request);
	connect(reply, SIGNAL(finished()), this, SLOT(onListReplyFinished()));
}

void PluginStatusManager::onListReplyFinished() {
	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply) {
		return;
	}

	m_requestInFlight = false;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		return;
	}

	QVariantMap body;
	std::string parseError;
	if (!JsonStd::parseObject(reply->readAll(), body, parseError)) {
		return;
	}

	const QVariantList plugins = body.value("plugins").toList();
	QVector<PluginStatus> result;
	result.reserve(plugins.size());
	for (const QVariant &value : plugins) {
		result.append(parsePlugin(value.toMap()));
	}
	emit pluginsUpdated(result);
}

QString PluginStatusManager::actionPath(Action action) {
	switch (action) {
		case Action::Start:   return "start";
		case Action::Stop:    return "stop";
		case Action::Restart: return "restart";
		case Action::Enable:  return "enable";
		case Action::Disable: return "disable";
	}
	return QString();
}

void PluginStatusManager::performAction(const QString &pluginId, Action action) {
	QNetworkRequest request(QUrl(baseUrl() + "/plugins/" + pluginId + "/" + actionPath(action)));
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}
	QNetworkReply *reply = m_networkManager->post(request, QByteArray());
	// Dynamic properties carry pluginId/action through to onActionReplyFinished()
	// -- old-style SIGNAL()/SLOT() connect (needed for Qt4/SDK4, see the
	// header comment) can't lambda-capture them the way this used to.
	reply->setProperty("dpb_pluginId", pluginId);
	reply->setProperty("dpb_action", static_cast<int>(action));
	connect(reply, SIGNAL(finished()), this, SLOT(onActionReplyFinished()));
}

void PluginStatusManager::onActionReplyFinished() {
	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply) {
		return;
	}
	const QString pluginId = reply->property("dpb_pluginId").toString();
	const Action action = static_cast<Action>(reply->property("dpb_action").toInt());
	reply->deleteLater();

	const bool success = reply->error() == QNetworkReply::NoError;
	QString errorMessage;
	if (!success) {
		QVariantMap body;
		std::string parseError;
		if (JsonStd::parseObject(reply->readAll(), body, parseError)) {
			errorMessage = body.value("detail").toString();
		}
		if (errorMessage.isEmpty()) {
			errorMessage = reply->errorString();
		}
	}

	emit actionFinished(pluginId, action, success, errorMessage);
	refresh();
}

// Manually included -- see the comment in DaemonHealthMonitor.cpp
// (daz-python-bridge-7wq).
#include "moc_PluginStatusManager.cpp"
