#pragma once

#include <QString>

// Resolves the on-disk layout the DPB daemon and its venvs live under:
//
//   <base>/bin/uv[.exe]           the vendored uv binary
//   <base>/run_venv/              isolated venv for the /run inline-scripting endpoint
//   <base>/plugins/<id>/venv/     one isolated venv per installed plugin, created eagerly
//                                 at install time by PluginDependencyInstaller, not here
//
// <base> is QStandardPaths::AppDataLocation + "/DazPythonBridge", i.e.
// AppData/Daz 3D/Studio4/DazPythonBridge on Windows, since the host DAZ Studio
// process has already set QCoreApplication's organization/application name.
class DaemonPaths {
public:
	static QString baseDir();
	static QString uvBinaryPath();
	static QString runVenvDir();
	static QString pluginVenvDir(const QString &pluginId);
	static QString pluginsDir();

	// Ensures baseDir() (and its bin/ subdirectory) exist on disk.
	// Returns false if creation failed.
	static bool ensureBaseDirExists();
};
