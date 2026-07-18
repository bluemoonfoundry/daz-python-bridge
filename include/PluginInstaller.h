#pragma once

#include <QObject>
#include <QString>

#include "PluginDependencyInstaller.h"
#include "ZipInstaller.h"

// Orchestrates a full plugin install (daz-python-bridge-sop.3 + sop.4):
// hardened zip extraction, then -- eagerly, as soon as the plugin directory
// is committed -- per-plugin venv creation and requirements.txt resolution.
//
// Dependency resolution is not deferred to the plugin's first invocation:
// running it here means a broken requirements.txt fails the install and is
// recorded in install_status.json (state: "failed") immediately, rather than
// surfacing later as a mysterious first-call failure.
class PluginInstaller : public QObject {
	Q_OBJECT
public:
	explicit PluginInstaller(QString pluginsDir, ZipInstaller::Limits limits = ZipInstaller::Limits(), QObject *parent = nullptr);

	// Extracts zipPath and, only on successful extraction, resolves its
	// dependencies. The extraction step is synchronous (as ZipInstaller
	// always has been); dependency resolution runs asynchronously via
	// QProcess and reports through finished().
	void install(const QString &zipPath);

signals:
	// Emitted immediately when extraction itself fails; no plugin directory
	// was created, so there is nothing for PluginDependencyInstaller to do.
	void extractionFailed(const QString &errorMessage);

	// Emitted exactly once per install() call, whether extraction or
	// dependency resolution is what ultimately failed.
	void finished(const QString &pluginId, bool success, const QString &errorMessage);

private:
	QString                     m_pluginsDir;
	ZipInstaller::Limits        m_limits;
	PluginDependencyInstaller  *m_depInstaller = nullptr;
};
