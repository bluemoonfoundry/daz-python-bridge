#include "PluginInstaller.h"

#include "DaemonProcess.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

PluginInstaller::PluginInstaller(QString pluginsDir, ZipInstaller::Limits limits, QObject *parent)
	: QObject(parent)
	, m_pluginsDir(std::move(pluginsDir))
	, m_limits(limits)
{
	m_networkManager = new QNetworkAccessManager(this);
}

QString PluginInstaller::readManifestVersion(const QString &manifestPath) {
	QFile manifestFile(manifestPath);
	if (!manifestFile.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const QJsonDocument doc = QJsonDocument::fromJson(manifestFile.readAll());
	if (!doc.isObject()) {
		return QString();
	}
	return doc.object().value(QStringLiteral("version")).toString();
}

void PluginInstaller::install(const QString &zipPath) {
	ZipInstaller zipInstaller(m_pluginsDir, m_limits);
	m_staged = zipInstaller.extractToStaging(zipPath);
	m_oldVersion = QString();  // reset: null until proven to be a reinstall below

	if (!m_staged.success) {
		emit extractionFailed(m_staged.errorMessage);
		emit finished(m_staged.pluginId, false, m_staged.errorMessage);
		return;
	}

	const QString finalDir = QDir(m_pluginsDir).filePath(m_staged.pluginId);
	if (!QFileInfo::exists(finalDir)) {
		commitAndResolveDeps();
		return;
	}

	// Reinstall: an id collision. Read the previous version before it's
	// displaced, then stop its warm worker -- best-effort, since a worker
	// that was never started (or a daemon that isn't running yet) is not a
	// reason to abandon the install; if a live worker really is still
	// holding a file lock, ZipInstaller::commit()'s rename will fail loudly
	// instead of silently corrupting the plugin directory.
	m_oldVersion = readManifestVersion(QDir(finalDir).filePath("manifest.json"));
	if (m_oldVersion.isNull()) {
		m_oldVersion = QStringLiteral("");  // "" (not null) now means "reinstall, no prior version"
	}

	QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1/plugins/%2/stop")
		.arg(DaemonProcess::kPort).arg(m_staged.pluginId)));
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}
	QNetworkReply *reply = m_networkManager->post(request, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		onStopReplyFinished(reply);
	});
}

void PluginInstaller::onStopReplyFinished(QNetworkReply *reply) {
	reply->deleteLater();
	// Errors (daemon not running, plugin never had a worker, auth mismatch,
	// ...) are not fatal here -- see the comment in install() -- so this
	// proceeds unconditionally to the file swap either way.
	commitAndResolveDeps();
}

void PluginInstaller::commitAndResolveDeps() {
	ZipInstaller zipInstaller(m_pluginsDir, m_limits);
	const ZipInstaller::CommitResult committed = zipInstaller.commit(m_staged.pluginId, m_staged.stagingDir);
	if (!committed.success) {
		QDir(m_staged.stagingDir).removeRecursively();
		emit finished(m_staged.pluginId, false, committed.errorMessage);
		return;
	}

	if (!m_oldVersion.isNull()) {
		emit reinstalled(m_staged.pluginId, m_oldVersion, m_staged.version);
	}

	m_depInstaller = new PluginDependencyInstaller(this);
	connect(m_depInstaller, &PluginDependencyInstaller::finished, this, &PluginInstaller::finished);
	m_depInstaller->run(m_staged.pluginId, committed.finalPluginDir);
}
