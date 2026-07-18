#include "DaemonPaths.h"

#include <QDir>

// QStandardPaths is Qt5+; Qt 4.8 (SDK4 DSS plugin, daz-python-bridge-7wq)
// only has QDesktopServices::storageLocation() for this, in QtGui rather
// than QtCore -- already linked into DzPythonBridge regardless of SDK
// version, so this needs no extra linkage.
#if DAZ_SDK_MAJOR_VERSION >= 6
#include <QStandardPaths>
#else
#include <QDesktopServices>
#endif

#ifdef Q_OS_WIN
static const char *kUvBinaryName = "uv.exe";
#else
static const char *kUvBinaryName = "uv";
#endif

QString DaemonPaths::baseDir() {
#if DAZ_SDK_MAJOR_VERSION >= 6
	const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
	const QString appData = QDesktopServices::storageLocation(QDesktopServices::DataLocation);
#endif
	return QDir(appData).filePath("DazPythonBridge");
}

QString DaemonPaths::uvBinaryPath() {
	return QDir(baseDir()).filePath(QString::fromLatin1("bin/%1").arg(kUvBinaryName));
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
