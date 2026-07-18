#include "PluginInstaller.h"

#include "DaemonProcess.h"
#include "JsonStd.h"
#include "PortableFs.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
	QVariantMap manifest;
	std::string parseError;
	if (!JsonStd::parseObject(manifestFile.readAll(), manifest, parseError)) {
		return QString();
	}
	return manifest.value(QString::fromLatin1("version")).toString();
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
	if (!QFileInfo(finalDir).exists()) {
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
		m_oldVersion = QString::fromLatin1("");  // "" (not null) now means "reinstall, no prior version"
	}

	QNetworkRequest request(QUrl(QString::fromLatin1("http://127.0.0.1:%1/plugins/%2/stop")
		.arg(DaemonProcess::kPort).arg(m_staged.pluginId)));
	if (!m_authToken.isEmpty()) {
		request.setRawHeader("X-DPB-Token", m_authToken.toUtf8());
	}
	QNetworkReply *reply = m_networkManager->post(request, QByteArray());
	// Old-style SIGNAL()/SLOT() connect -- see the header comment on
	// onStopReplyFinished().
	connect(reply, SIGNAL(finished()), this, SLOT(onStopReplyFinished()));
}

void PluginInstaller::onStopReplyFinished() {
	QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply) {
		return;
	}
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
		PortableFs::removeRecursively(m_staged.stagingDir);
		emit finished(m_staged.pluginId, false, committed.errorMessage);
		return;
	}

	if (!m_oldVersion.isNull()) {
		emit reinstalled(m_staged.pluginId, m_oldVersion, m_staged.version);
	}

	m_depInstaller = new PluginDependencyInstaller(this);
	// Signal-to-signal forwarding via old-style SIGNAL()/SIGNAL() connect --
	// this class is compiled against Qt 4.8 for the SDK4 DSS plugin
	// (daz-python-bridge-7wq), which has no PMF-based connect().
	connect(m_depInstaller, SIGNAL(finished(QString, bool, QString)),
	        this, SIGNAL(finished(QString, bool, QString)));
	m_depInstaller->run(m_staged.pluginId, committed.finalPluginDir);
}

// Manually included -- see the comment in DaemonHealthMonitor.cpp
// (daz-python-bridge-7wq).
#include "moc_PluginInstaller.cpp"
