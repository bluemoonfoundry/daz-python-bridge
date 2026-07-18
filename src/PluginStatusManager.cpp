#include "PluginStatusManager.h"

#include "DaemonProcess.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace {

QString baseUrl() {
	return QStringLiteral("http://127.0.0.1:%1").arg(DaemonProcess::kPort);
}

PluginStatus parsePlugin(const QJsonObject &obj) {
	PluginStatus status;
	status.pluginId = obj.value("plugin_id").toString();
	status.state = obj.value("state").toString();
	status.pid = obj.value("pid").isNull() ? -1 : (qint64)obj.value("pid").toDouble();
	status.memoryBytes = obj.value("memory_bytes").isNull() ? -1 : (qint64)obj.value("memory_bytes").toDouble();
	status.lastUsed = obj.value("last_used").isNull() ? 0.0 : obj.value("last_used").toDouble();
	return status;
}

} // namespace

PluginStatusManager::PluginStatusManager(QObject *parent) : QObject(parent) {
	m_networkManager = new QNetworkAccessManager(this);
	connect(&m_timer, &QTimer::timeout, this, &PluginStatusManager::refresh);
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
	QNetworkReply *reply = m_networkManager->get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		onListReplyFinished(reply);
	});
}

void PluginStatusManager::onListReplyFinished(QNetworkReply *reply) {
	m_requestInFlight = false;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		return;
	}

	const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
	if (!doc.isObject()) {
		return;
	}

	const QJsonArray plugins = doc.object().value("plugins").toArray();
	QVector<PluginStatus> result;
	result.reserve(plugins.size());
	for (const QJsonValue &value : plugins) {
		result.append(parsePlugin(value.toObject()));
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
	QNetworkReply *reply = m_networkManager->post(request, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [this, reply, pluginId, action]() {
		onActionReplyFinished(reply, pluginId, action);
	});
}

void PluginStatusManager::onActionReplyFinished(QNetworkReply *reply, const QString &pluginId, Action action) {
	reply->deleteLater();

	const bool success = reply->error() == QNetworkReply::NoError;
	QString errorMessage;
	if (!success) {
		const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
		errorMessage = doc.isObject() ? doc.object().value("detail").toString() : reply->errorString();
		if (errorMessage.isEmpty()) {
			errorMessage = reply->errorString();
		}
	}

	emit actionFinished(pluginId, action, success, errorMessage);
	refresh();
}
