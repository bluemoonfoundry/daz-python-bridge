#include "UvBootstrapper.h"

#include "DaemonPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>

UvBootstrapper::UvBootstrapper(QObject *parent) : QObject(parent) {}

void UvBootstrapper::ensureReady() {
	if (m_daemonSourceDir.isEmpty()) {
		fail("ensure-base-dir", QString::fromLatin1(
			"setDaemonSourceDir() was not called before ensureReady()"));
		return;
	}
	if (!DaemonPaths::ensureBaseDirExists()) {
		fail("ensure-base-dir", QString::fromLatin1("could not create %1").arg(DaemonPaths::baseDir()));
		return;
	}
	installUv();
}

void UvBootstrapper::fail(const QString &step, const QString &message) {
	emit stepFailed(step, message);
}

void UvBootstrapper::fail(const QString &step, QProcess *process) {
	const QString message = QString::fromUtf8(process->readAllStandardError());
	emit stepFailed(step, message.isEmpty() ? process->errorString() : message);
}

QString UvBootstrapper::venvPythonPath() const {
#ifdef Q_OS_WIN
	return QDir(DaemonPaths::runVenvDir()).filePath("Scripts/python.exe");
#else
	return QDir(DaemonPaths::runVenvDir()).filePath("bin/python");
#endif
}

// ─── Step 1: install uv itself ────────────────────────────────────────────────

void UvBootstrapper::installUv() {
	if (QFileInfo(DaemonPaths::uvBinaryPath()).exists()) {
		installPython();
		return;
	}

	emit stepStarted("install-uv");

	m_process = new QProcess(this);

	QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
	env.insert("UV_INSTALL_DIR", QDir(DaemonPaths::baseDir()).filePath("bin"));
	env.insert("UV_NO_MODIFY_PATH", "1");
	m_process->setProcessEnvironment(env);

#ifdef Q_OS_WIN
	const QString program = "powershell.exe";
	// QStringList() << ... , not a brace-init list: QStringList's
	// initializer-list constructor is Qt5+ only (daz-python-bridge-7wq).
	const QStringList arguments = QStringList()
		<< "-NoProfile" << "-ExecutionPolicy" << "Bypass" << "-Command"
		<< "irm https://astral.sh/uv/install.ps1 | iex";
#else
	const QString program = "/bin/sh";
	const QStringList arguments = QStringList() << "-c" << "curl -LsSf https://astral.sh/uv/install.sh | sh";
#endif

	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onInstallUvFinished(int, QProcess::ExitStatus)));
	m_process->start(program, arguments);
}

void UvBootstrapper::onInstallUvFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0 || !QFileInfo(DaemonPaths::uvBinaryPath()).exists()) {
		fail("install-uv", m_process);
		return;
	}
	emit stepSucceeded("install-uv");
	installPython();
}

// ─── Step 2: install Python 3.11 via uv ──────────────────────────────────────

void UvBootstrapper::installPython() {
	emit stepStarted("install-python");

	m_process = new QProcess(this);
	const QStringList arguments = QStringList() << "python" << "install" << "3.11";

	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onInstallPythonFinished(int, QProcess::ExitStatus)));
	m_process->start(DaemonPaths::uvBinaryPath(), arguments);
}

void UvBootstrapper::onInstallPythonFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0) {
		fail("install-python", m_process);
		return;
	}
	emit stepSucceeded("install-python");
	createVenv();
}

// ─── Step 3: create run_venv, isolated from any plugin venv ─────────────────

void UvBootstrapper::createVenv() {
	if (QFileInfo(venvPythonPath()).exists()) {
		installDeps();
		return;
	}

	emit stepStarted("create-venv");

	m_process = new QProcess(this);
	const QStringList arguments = QStringList() << "venv" << DaemonPaths::runVenvDir() << "--python" << "3.11";

	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onCreateVenvFinished(int, QProcess::ExitStatus)));
	m_process->start(DaemonPaths::uvBinaryPath(), arguments);
}

void UvBootstrapper::onCreateVenvFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0) {
		fail("create-venv", m_process);
		return;
	}
	emit stepSucceeded("create-venv");
	installDeps();
}

// ─── Step 4: install the daemon package itself (+ its deps) into run_venv ───
// Installs daemonSourceDir as a real package rather than naming fastapi/
// uvicorn by hand, so `daemon` (the module uvicorn is launched against, see
// DaemonProcess) is genuinely importable regardless of the daemon process's
// working directory. --reinstall so edits to the bundled daemon source are
// picked up on every launch, not just the first time the venv is created.

void UvBootstrapper::installDeps() {
	emit stepStarted("install-deps");

	m_process = new QProcess(this);
	const QStringList arguments = QStringList()
		<< "pip" << "install" << "--python" << venvPythonPath()
		<< "--reinstall" << m_daemonSourceDir;

	connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)),
	        this, SLOT(onInstallDepsFinished(int, QProcess::ExitStatus)));
	m_process->start(DaemonPaths::uvBinaryPath(), arguments);
}

void UvBootstrapper::onInstallDepsFinished(int exitCode, QProcess::ExitStatus status) {
	if (status != QProcess::NormalExit || exitCode != 0) {
		fail("install-deps", m_process);
		return;
	}
	emit stepSucceeded("install-deps");
	emit ready();
}

// Manually included -- see the comment in DaemonHealthMonitor.cpp
// (daz-python-bridge-7wq).
#include "moc_UvBootstrapper.cpp"
