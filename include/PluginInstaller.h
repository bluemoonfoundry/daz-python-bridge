#pragma once

#include <QObject>
#include <QString>

#include "PluginDependencyInstaller.h"
#include "ZipInstaller.h"

class QNetworkAccessManager;
class QNetworkReply;

// Orchestrates a full plugin install (daz-python-bridge-sop.3 + sop.4),
// including reinstalling over an existing plugin id (daz-python-bridge-sop.8):
// hardened zip extraction, then -- eagerly, as soon as the plugin directory
// is committed -- per-plugin venv creation and requirements.txt resolution.
//
// Dependency resolution is not deferred to the plugin's first invocation:
// running it here means a broken requirements.txt fails the install and is
// recorded in install_status.json (state: "failed") immediately, rather than
// surfacing later as a mysterious first-call failure.
//
// Reinstalling over an existing plugin id (matched by manifest.json's "id")
// follows the same overwrite-by-id convention as DSS's own script registry
// (RegisterScript on port 18811): the new drop simply replaces the old one,
// no version-bump prompt. The one DPB-specific wrinkle is that a live
// plugin's warm worker (daemon/worker_manager.py) holds a python.exe process
// that locks files inside its own venv on Windows, so that worker has to be
// stopped -- via the daemon's own POST /plugins/{id}/stop -- before the
// files underneath it can be swapped out.
class PluginInstaller : public QObject {
	Q_OBJECT
public:
	explicit PluginInstaller(QString pluginsDir, ZipInstaller::Limits limits = ZipInstaller::Limits(), QObject *parent = nullptr);

	// Attaches X-DPB-Token to the POST /plugins/{id}/stop call this class
	// makes when reinstalling over an existing plugin id (daz-python-bridge-sop.7).
	void setAuthToken(const QString &token) { m_authToken = token; }

	// Extracts zipPath and, only on successful extraction, resolves its
	// dependencies. The extraction step runs synchronously (as ZipInstaller
	// always has); if zipPath's manifest id matches an already-installed
	// plugin, an async call to stop that plugin's warm worker is made first,
	// best-effort, before the file swap. Dependency resolution runs
	// asynchronously via QProcess and reports through finished().
	void install(const QString &zipPath);

signals:
	// Emitted immediately when extraction itself fails; no plugin directory
	// was created, so there is nothing for PluginDependencyInstaller to do.
	void extractionFailed(const QString &errorMessage);

	// Emitted exactly once, only when install() replaced an already-installed
	// plugin id, right after the file swap lands (before dependency
	// resolution). oldVersion/newVersion come from manifest.json's optional
	// "version" field and are "" when a manifest had none.
	void reinstalled(const QString &pluginId, const QString &oldVersion, const QString &newVersion);

	// Emitted exactly once per install() call, whether extraction or
	// dependency resolution is what ultimately failed.
	void finished(const QString &pluginId, bool success, const QString &errorMessage);

private:
	void commitAndResolveDeps();
	void onStopReplyFinished(QNetworkReply *reply);
	static QString readManifestVersion(const QString &manifestPath);

	QString                     m_pluginsDir;
	ZipInstaller::Limits        m_limits;
	QString                     m_authToken;
	QNetworkAccessManager      *m_networkManager = nullptr;
	PluginDependencyInstaller  *m_depInstaller = nullptr;

	// In-flight install state, valid between install() and the eventual
	// finished() -- this class only ever has one install running at a time.
	ZipInstaller::StageResult   m_staged;
	QString                     m_oldVersion;  // null (not "") means "this is not a reinstall"
};
