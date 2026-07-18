#include "PluginDependencyInstaller.h"

#include "DaemonPaths.h"
#include "JsonStd.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

PluginDependencyInstaller::PluginDependencyInstaller(QObject *parent) : QObject(parent) {}

void PluginDependencyInstaller::run(const QString &pluginId, const QString &pluginDir) {
	m_pluginId = pluginId;
	m_pluginDir = pluginDir;
	createVenv();
}

void PluginDependencyInstaller::fail(const QString &step, const QString &message) {
	emit stepFailed(step, message);
	writeStatusFile(false, step, message);
	emit finished(m_pluginId, false, message);
}

void PluginDependencyInstaller::fail(const QString &step, QProcess *process) {
	const QString message = QString::fromUtf8(process->readAllStandardError());
	fail(step, message.isEmpty() ? process->errorString() : message);
}

void PluginDependencyInstaller::succeed() {
	writeStatusFile(true, QString(), QString());
	emit finished(m_pluginId, true, QString());
}

QString PluginDependencyInstaller::venvPythonPath() const {
	const QString venvDir = DaemonPaths::pluginVenvDir(m_pluginId);
#ifdef Q_OS_WIN
	return QDir(venvDir).filePath("Scripts/python.exe");
#else
	return QDir(venvDir).filePath("bin/python");
#endif
}

QString PluginDependencyInstaller::requirementsPath() const {
	return QDir(m_pluginDir).filePath("requirements.txt");
}

void PluginDependencyInstaller::writeStatusFile(bool success, const QString &step, const QString &errorMessage) const {
	QVariantMap status;
	status["state"] = success ? QString::fromLatin1("ok") : QString::fromLatin1("failed");
	if (!success) {
		status["step"] = step;
		status["error"] = errorMessage;
	}

	QFile statusFile(QDir(m_pluginDir).filePath("install_status.json"));
	if (statusFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		statusFile.write(JsonStd::variantToJsonBytes(status));
	}
}

// ─── Step 1: create the plugin's own isolated venv ──────────────────────────

void PluginDependencyInstaller::createVenv() {
	emit stepStarted("create-venv");

	m_process = new QProcess(this);
	// --clear: a reinstall over an existing plugin id (daz-python-bridge-sop.8)
	// re-runs dependency resolution against the *new* requirements.txt, but
	// the venv directory itself lives outside the plugin's zip-extracted
	// tree (under DaemonPaths::pluginVenvDir), so ZipInstaller's file swap
	// never touches it -- without --clear, `uv venv` refuses to recreate an
	// already-existing venv. Harmless no-op for a first install.
	// QStringList() << ... , not a brace-init list: QStringList's
	// initializer-list constructor is Qt5+ only (daz-python-bridge-7wq).
	const QStringList arguments = QStringList()
		<< "venv" << DaemonPaths::pluginVenvDir(m_pluginId) << "--python" << "3.11" << "--clear";

	// Old-style SIGNAL()/SLOT() macro connect, not the PMF-based connect()
	// syntax: this class is compiled against Qt 4.8 for the SDK4 DSS plugin
	// (daz-python-bridge-7wq), which predates Qt5's function-pointer connect
	// entirely. Works identically under Qt6.
	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onCreateVenvFinished(int, QProcess::ExitStatus)));
	// start(program, arguments) in one call -- see DaemonProcess.cpp's
	// comment on why, not setProgram()+setArguments()+start().
	m_process->start(DaemonPaths::uvBinaryPath(), arguments);
}

void PluginDependencyInstaller::onCreateVenvFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0) {
		fail("create-venv", m_process);
		return;
	}
	emit stepSucceeded("create-venv");
	installDeps();
}

// ─── Step 2: resolve requirements.txt into that venv, if the plugin has one ─

void PluginDependencyInstaller::installDeps() {
	if (!QFileInfo(requirementsPath()).exists()) {
		succeed();
		return;
	}

	emit stepStarted("install-deps");

	m_process = new QProcess(this);
	const QStringList arguments = QStringList()
		<< "pip" << "install" << "--python" << venvPythonPath()
		<< "-r" << requirementsPath();

	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onInstallDepsFinished(int, QProcess::ExitStatus)));
	m_process->start(DaemonPaths::uvBinaryPath(), arguments);
}

void PluginDependencyInstaller::onInstallDepsFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0) {
		fail("install-deps", m_process);
		return;
	}
	emit stepSucceeded("install-deps");
	succeed();
}

// Manually included -- see the comment in DaemonHealthMonitor.cpp
// (daz-python-bridge-7wq).
#include "moc_PluginDependencyInstaller.cpp"
