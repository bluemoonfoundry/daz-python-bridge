#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

// Per-plugin venv creation and eager dependency resolution
// (daz-python-bridge-sop.4).
//
// Run right after ZipInstaller lands a plugin directory -- either a fresh
// install, or PluginInstaller re-running this after a reinstall over an
// existing plugin id (daz-python-bridge-sop.8): creates an isolated venv at
// DaemonPaths::pluginVenvDir(pluginId), bound to the same Python 3.11 build
// UvBootstrapper already downloaded (so creation is sub-second), then -- if
// the plugin ships a requirements.txt -- installs it into that venv via
// `uv pip install -r`. Two plugins pinning conflicting dependency versions
// cannot clobber each other because each gets its own venv rather than a
// shared site-packages. venv creation always passes --clear so a reinstall's
// fresh resolution isn't left holding stale packages from the previous one
// (the venv directory lives outside the plugin's zip-extracted tree, so a
// reinstall's file swap never touches it on its own).
//
// Resolution is eager, not lazy: run() is meant to be called immediately
// after extraction, so a broken requirements.txt is caught and recorded as a
// failed install status right away instead of surfacing later on the
// plugin's first invocation. The resulting install_status.json written into
// the plugin dir is what daz-python-bridge-sop.6's status pane is expected
// to read (state: "ok" | "failed").
class PluginDependencyInstaller : public QObject {
	Q_OBJECT
public:
	explicit PluginDependencyInstaller(QObject *parent = nullptr);

	// Kicks off venv creation + dependency install for the plugin at
	// pluginDir (already extracted by ZipInstaller, named pluginId). Emits
	// finished() exactly once when done; never blocks the caller's thread.
	void run(const QString &pluginId, const QString &pluginDir);

signals:
	// step is "create-venv" or "install-deps".
	void stepStarted(const QString &step);
	void stepSucceeded(const QString &step);
	void stepFailed(const QString &step, const QString &errorMessage);

	// Emitted exactly once, after install_status.json has been written.
	void finished(const QString &pluginId, bool success, const QString &errorMessage);

private slots:
	void onCreateVenvFinished(int exitCode, QProcess::ExitStatus status);
	void onInstallDepsFinished(int exitCode, QProcess::ExitStatus status);

private:
	void createVenv();
	void installDeps();
	void fail(const QString &step, const QString &message);
	void fail(const QString &step, QProcess *process);
	void succeed();
	QString venvPythonPath() const;
	QString requirementsPath() const;
	void writeStatusFile(bool success, const QString &step, const QString &errorMessage) const;

	QString    m_pluginId;
	QString    m_pluginDir;
	QProcess  *m_process = nullptr;
};
