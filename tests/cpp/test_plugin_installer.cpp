// Manual/CI test harness for PluginInstaller's reinstall-over-an-existing-id
// flow (daz-python-bridge-sop.8).
//
// No daemon is running in this harness, so the POST /plugins/{id}/stop call
// PluginInstaller makes before reinstalling fails fast (connection refused)
// -- exercising the "best-effort, proceed regardless" path documented in
// PluginInstaller.cpp: what matters here is that the file swap and
// dependency re-resolution still complete correctly, and that reinstalled()
// fires with the right before/after version once (empty for this fixture,
// which ships no manifest.json "version" field).
//
// Requires `uv` and network access (venv creation resolves a Python 3.11
// build the first time), matching test_plugin_dependency_installer.cpp's
// constraints. Not wired into CTest; run directly and check the exit code.

#include "PluginInstaller.h"
#include "DaemonPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>

namespace {

int g_failures = 0;

void expectTrue(bool condition, const char *what)
{
	if (!condition) {
		++g_failures;
		std::printf("FAIL: %s\n", what);
	} else {
		std::printf("ok:   %s\n", what);
	}
}

QString fixturesDir()
{
	QFileInfo self(QString::fromUtf8(__FILE__));
	return QDir(self.absolutePath()).filePath("../cpp_fixtures");
}

// Runs installer.install(zipPath) and pumps a local event loop until
// finished() fires or a generous timeout elapses (network-bound: uv venv
// creation may need to resolve a Python 3.11 build).
bool runAndWait(PluginInstaller &installer, const QString &zipPath,
                bool *outSuccess, QString *outError,
                bool *outReinstalled, QString *outOldVersion, QString *outNewVersion)
{
	QEventLoop loop;
	bool gotFinished = false;
	QObject::connect(&installer, &PluginInstaller::finished,
		[&](const QString &, bool success, const QString &error) {
			gotFinished = true;
			*outSuccess = success;
			*outError = error;
			loop.quit();
		});
	QObject::connect(&installer, &PluginInstaller::reinstalled,
		[&](const QString &, const QString &oldVersion, const QString &newVersion) {
			*outReinstalled = true;
			*outOldVersion = oldVersion;
			*outNewVersion = newVersion;
		});

	QTimer timeoutTimer;
	timeoutTimer.setSingleShot(true);
	QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeoutTimer.start(180000);  // 3 minutes: uv python install

	installer.install(zipPath);
	loop.exec();
	return gotFinished;
}

}  // namespace

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	// Same sandboxing as test_plugin_dependency_installer.cpp: PluginInstaller
	// hands off to PluginDependencyInstaller, which resolves venv paths under
	// QStandardPaths::AppDataLocation.
	QStandardPaths::setTestModeEnabled(true);

	if (!DaemonPaths::ensureBaseDirExists()) {
		std::printf("FAIL: could not create sandboxed base dir\n");
		return 1;
	}
	const QString systemUv = QStandardPaths::findExecutable("uv");
	if (systemUv.isEmpty()) {
		std::printf("FAIL: no `uv` found on PATH -- required to run this test\n");
		return 1;
	}
	if (!QFile::copy(systemUv, DaemonPaths::uvBinaryPath())) {
		std::printf("FAIL: could not stage uv binary into sandboxed base dir\n");
		return 1;
	}

	QTemporaryDir pluginsDir;
	expectTrue(pluginsDir.isValid(), "temp plugins dir created");
	const QString zipPath = QDir(fixturesDir()).filePath("valid_plugin.zip");
	const QString finalDir = QDir(pluginsDir.path()).filePath("valid_plugin");

	// ─── Fresh install: no reinstalled() signal ────────────────────────────
	{
		PluginInstaller installer(pluginsDir.path());
		bool success = false;
		QString error;
		bool gotReinstalled = false;
		QString oldVersion, newVersion;

		expectTrue(runAndWait(installer, zipPath, &success, &error, &gotReinstalled, &oldVersion, &newVersion),
			"fresh install finished within timeout");
		if (!success)
			std::printf("     error: %s\n", qPrintable(error));
		expectTrue(success, "fresh install succeeds");
		expectTrue(!gotReinstalled, "reinstalled() does not fire for a fresh install");
		expectTrue(QFileInfo::exists(QDir(finalDir).filePath("manifest.json")),
			"plugin files landed on disk");
		expectTrue(QFileInfo::exists(DaemonPaths::pluginVenvDir("valid_plugin")),
			"plugin venv was created");
	}

	// Mark the install with a file that must NOT survive the reinstall below,
	// proving a real file swap happened (not a no-op skip).
	{
		QFile marker(QDir(finalDir).filePath("stale_marker.txt"));
		expectTrue(marker.open(QIODevice::WriteOnly), "marker file opens");
		marker.write("pre-reinstall");
	}

	// ─── Reinstall over the same id: file swap + reinstalled() signal ─────
	{
		PluginInstaller installer(pluginsDir.path());
		bool success = false;
		QString error;
		bool gotReinstalled = false;
		QString oldVersion, newVersion;

		expectTrue(runAndWait(installer, zipPath, &success, &error, &gotReinstalled, &oldVersion, &newVersion),
			"reinstall finished within timeout");
		if (!success)
			std::printf("     error: %s\n", qPrintable(error));
		expectTrue(success, "reinstall succeeds (no live worker was actually holding a lock in this harness)");
		expectTrue(gotReinstalled, "reinstalled() fires when the id already existed");
		expectTrue(oldVersion.isEmpty() && newVersion.isEmpty(),
			"old/new version are both empty -- this fixture's manifest.json has no \"version\" field");
		expectTrue(!QFileInfo::exists(QDir(finalDir).filePath("stale_marker.txt")),
			"the pre-reinstall marker file did not survive -- a real overwrite happened");
		expectTrue(QFileInfo::exists(QDir(finalDir).filePath("manifest.json")),
			"reinstalled plugin's files are present");
	}

	std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
	return g_failures == 0 ? 0 : 1;
}
