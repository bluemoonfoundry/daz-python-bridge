#include "DaemonPaths.h"

#include <QDir>
#include <QStandardPaths>

#ifdef Q_OS_WIN
static const char *kUvBinaryName = "uv.exe";
#else
static const char *kUvBinaryName = "uv";
#endif

QString DaemonPaths::baseDir() {
	const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	return QDir(appData).filePath("DazPythonBridge");
}

QString DaemonPaths::uvBinaryPath() {
	return QDir(baseDir()).filePath(QStringLiteral("bin/%1").arg(kUvBinaryName));
}

QString DaemonPaths::runVenvDir() {
	return QDir(baseDir()).filePath("run_venv");
}

QString DaemonPaths::pluginsDir() {
	return QDir(baseDir()).filePath("plugins");
}

QString DaemonPaths::pluginVenvDir(const QString &pluginId) {
	return QDir(pluginsDir()).filePath(pluginId + "/venv");
}

bool DaemonPaths::ensureBaseDirExists() {
	QDir dir;
	return dir.mkpath(baseDir()) && dir.mkpath(QDir(baseDir()).filePath("bin"));
}
