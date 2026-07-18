#include "PluginDependencyInstaller.h"

#include "DaemonPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

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
	QJsonObject status;
	status["state"] = success ? QStringLiteral("ok") : QStringLiteral("failed");
	if (!success) {
		status["step"] = step;
		status["error"] = errorMessage;
	}

	QFile statusFile(QDir(m_pluginDir).filePath("install_status.json"));
	if (statusFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		statusFile.write(QJsonDocument(status).toJson());
	}
}

// ─── Step 1: create the plugin's own isolated venv ──────────────────────────

void PluginDependencyInstaller::createVenv() {
	emit stepStarted("create-venv");

	m_process = new QProcess(this);
	m_process->setProgram(DaemonPaths::uvBinaryPath());
	// --clear: a reinstall over an existing plugin id (daz-python-bridge-sop.8)
	// re-runs dependency resolution against the *new* requirements.txt, but
	// the venv directory itself lives outside the plugin's zip-extracted
	// tree (under DaemonPaths::pluginVenvDir), so ZipInstaller's file swap
	// never touches it -- without --clear, `uv venv` refuses to recreate an
	// already-existing venv. Harmless no-op for a first install.
	m_process->setArguments({"venv", DaemonPaths::pluginVenvDir(m_pluginId), "--python", "3.11", "--clear"});

	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
	        this, &PluginDependencyInstaller::onCreateVenvFinished);
	m_process->start();
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
	if (!QFileInfo::exists(requirementsPath())) {
		succeed();
		return;
	}

	emit stepStarted("install-deps");

	m_process = new QProcess(this);
	m_process->setProgram(DaemonPaths::uvBinaryPath());
	m_process->setArguments({
		"pip", "install", "--python", venvPythonPath(),
		"-r", requirementsPath()
	});

	connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
	        this, &PluginDependencyInstaller::onInstallDepsFinished);
	m_process->start();
}

void PluginDependencyInstaller::onInstallDepsFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0) {
		fail("install-deps", m_process);
		return;
	}
	emit stepSucceeded("install-deps");
	succeed();
}
