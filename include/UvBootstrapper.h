#pragma once

#include <QObject>
#include <QProcess>

// First-run JIT bootstrap for the DPB daemon:
//   1. Install uv (via astral's official install script) into DaemonPaths::baseDir()/bin
//      if not already present there.
//   2. `uv python install 3.11`
//   3. `uv venv` DaemonPaths::runVenvDir() bound to Python 3.11
//   4. `uv pip install <daemonSourceDir>` into that venv -- installs the
//      actual daemon/ package (plus its fastapi/uvicorn/psutil dependencies,
//      all declared in daemonSourceDir's pyproject.toml) as a real importable
//      package, rather than the daemon module needing to sit on the process's
//      cwd/PYTHONPATH. See setDaemonSourceDir().
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

	// Directory containing the daemon's own source (a daemon/ package
	// alongside pyproject.toml) that step 4 installs into run_venv. Must be
	// set before ensureReady() is called. The DSS-facing caller resolves this
	// via dzApp->getResourcesPath() + "/BlueMoonFoundry/DazPythonBridge" --
	// this class has no DAZ SDK dependency of its own, so it takes the
	// resolved path as a plain string instead (daz-python-bridge-5l4 follow-up:
	// build.sh install bundles daemon/ + pyproject.toml there, mirroring how
	// other BlueMoonFoundry DSS plugins ship external Python resources).
	void setDaemonSourceDir(const QString &dir) { m_daemonSourceDir = dir; }

	// Idempotent — safe to call repeatedly (e.g. on every pane open); steps
	// already satisfied are skipped. install-deps (step 4) always re-runs
	// uv pip install, so daemon source edits are picked up on every launch.
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
	QString m_daemonSourceDir;
};
