#pragma once

#include <QObject>
#include <QProcess>

// First-run JIT bootstrap for the DPB daemon:
//   1. Install uv (via astral's official install script) into DaemonPaths::baseDir()/bin
//      if not already present there.
//   2. `uv python install 3.11`
//   3. `uv venv` DaemonPaths::runVenvDir() bound to Python 3.11
//   4. `uv pip install` fastapi + uvicorn into that venv
//
// Per-plugin venvs are NOT created here — see PluginDependencyInstaller, which
// creates one per installed plugin, eagerly at install time.
//
// Every step runs a QProcess asynchronously (never blocks the caller's thread);
// steps are chained off QProcess::finished so this is safe to drive from the
// Qt main thread. Call ensureReady() once; it no-ops the steps that are
// already satisfied (uv present, run_venv already created with deps installed).
class UvBootstrapper : public QObject {
	Q_OBJECT
public:
	explicit UvBootstrapper(QObject *parent = nullptr);

	// Idempotent — safe to call repeatedly (e.g. on every pane open); steps
	// already satisfied are skipped.
	void ensureReady();

signals:
	// Emitted once uv is installed, Python 3.11 is available, and run_venv
	// has fastapi/uvicorn installed. The daemon can now be launched.
	void ready();

	// step is a short machine-readable stage name (e.g. "install-uv",
	// "install-python", "create-venv", "install-deps") for status reporting.
	void stepFailed(const QString &step, const QString &errorMessage);
	void stepStarted(const QString &step);
	void stepSucceeded(const QString &step);

private slots:
	void onInstallUvFinished(int exitCode, QProcess::ExitStatus status);
	void onInstallPythonFinished(int exitCode, QProcess::ExitStatus status);
	void onCreateVenvFinished(int exitCode, QProcess::ExitStatus status);
	void onInstallDepsFinished(int exitCode, QProcess::ExitStatus status);

private:
	void installUv();
	void installPython();
	void createVenv();
	void installDeps();
	QString venvPythonPath() const;
	void fail(const QString &step, const QString &message);
	void fail(const QString &step, QProcess *process);

	QProcess *m_process = nullptr;
};
