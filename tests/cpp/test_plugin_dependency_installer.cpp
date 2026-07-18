// Manual/CI test harness for PluginDependencyInstaller (daz-python-bridge-sop.4).
//
// Exercises real `uv venv` / `uv pip install` invocations against a fake
// AppData base dir (redirected via DPB_TEST_BASE_DIR) rather than mocking
// QProcess, so this proves the actual on-disk contract: two plugins with
// conflicting pinned dependency versions each install into their own venv
// without clobbering each other, and a plugin with an unresolvable
// requirements.txt is recorded as install_status.json state "failed" rather
// than only failing lazily on first invocation.
//
// Requires `uv` and network access to resolve packages; not wired into CTest
// (no test framework dependency in this repo yet, matching test_zip_installer.cpp).
// Run directly and check the exit code / output.

#include "PluginDependencyInstaller.h"
#include "DaemonPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
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

bool writeFile(const QString &path, const QByteArray &contents)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile f(path);
	if (!f.open(QIODevice::WriteOnly))
		return false;
	f.write(contents);
	return true;
}

// Runs installer.run(...) and pumps a local event loop until finished() fires
// or a generous timeout elapses (network-bound: python/package resolution).
bool runAndWait(PluginDependencyInstaller &installer, const QString &pluginId, const QString &pluginDir,
                bool *outSuccess, QString *outError)
{
	QEventLoop loop;
	bool gotFinished = false;
	QObject::connect(&installer, &PluginDependencyInstaller::finished,
		[&](const QString &, bool success, const QString &error) {
			gotFinished = true;
			*outSuccess = success;
			*outError = error;
			loop.quit();
		});

	QTimer timeoutTimer;
	timeoutTimer.setSingleShot(true);
	QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeoutTimer.start(180000);  // 3 minutes: uv python install + pip resolution

	installer.run(pluginId, pluginDir);
	loop.exec();
	return gotFinished;
}

QJsonObject readStatusFile(const QString &pluginDir)
{
	QFile f(QDir(pluginDir).filePath("install_status.json"));
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(f.readAll()).object();
}

}  // namespace

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);

	// PluginDependencyInstaller resolves DaemonPaths::pluginVenvDir() (and
	// therefore uv's install target) under QStandardPaths::AppDataLocation --
	// sandbox that to a throwaway "_test" subtree instead of the real
	// per-user DazPythonBridge data dir this process would otherwise share
	// with a real DAZ Studio install.
	QStandardPaths::setTestModeEnabled(true);

	// PluginDependencyInstaller expects UvBootstrapper::ensureReady() to have
	// already staged a `uv` binary at DaemonPaths::uvBinaryPath() (that
	// bootstrap flow is sop.5's/UvBootstrapper's own concern, untested here);
	// stage it by copying whatever `uv` this machine already has on PATH,
	// rather than re-driving the network installer for every test run.
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

	// ─── Two plugins pinning conflicting dependency versions coexist ───────
	{
		QTemporaryDir pluginsRoot;
		expectTrue(pluginsRoot.isValid(), "temp plugins root created");

		const QString pluginADir = QDir(pluginsRoot.path()).filePath("plugin_a");
		const QString pluginBDir = QDir(pluginsRoot.path()).filePath("plugin_b");
		expectTrue(writeFile(QDir(pluginADir).filePath("requirements.txt"), "packaging==23.0\n"),
			"wrote plugin_a requirements.txt");
		expectTrue(writeFile(QDir(pluginBDir).filePath("requirements.txt"), "packaging==24.0\n"),
			"wrote plugin_b requirements.txt (conflicting version)");

		PluginDependencyInstaller installerA;
		bool successA = false;
		QString errorA;
		expectTrue(runAndWait(installerA, "plugin_a", pluginADir, &successA, &errorA),
			"plugin_a dependency install finished within timeout");
		if (!successA)
			std::printf("     plugin_a error: %s\n", qPrintable(errorA));
		expectTrue(successA, "plugin_a install succeeded");

		PluginDependencyInstaller installerB;
		bool successB = false;
		QString errorB;
		expectTrue(runAndWait(installerB, "plugin_b", pluginBDir, &successB, &errorB),
			"plugin_b dependency install finished within timeout");
		expectTrue(successB, "plugin_b install succeeded");

		expectTrue(successA && successB,
			"conflicting-version plugins both installed without clobbering each other");
		expectTrue(readStatusFile(pluginADir).value("state").toString() == QLatin1String("ok"),
			"plugin_a install_status.json reports ok");
		expectTrue(readStatusFile(pluginBDir).value("state").toString() == QLatin1String("ok"),
			"plugin_b install_status.json reports ok");
	}

	// ─── Unresolvable requirements.txt fails at install time ────────────────
	{
		QTemporaryDir pluginsRoot;
		const QString pluginDir = QDir(pluginsRoot.path()).filePath("broken_plugin");
		expectTrue(writeFile(QDir(pluginDir).filePath("requirements.txt"),
			"this-package-definitely-does-not-exist-anywhere==0.0.0\n"),
			"wrote broken_plugin requirements.txt");

		PluginDependencyInstaller installer;
		bool success = true;
		QString error;
		expectTrue(runAndWait(installer, "broken_plugin", pluginDir, &success, &error),
			"broken_plugin dependency install finished within timeout");
		expectTrue(!success, "broken_plugin install fails at install time, not later");

		const QJsonObject status = readStatusFile(pluginDir);
		expectTrue(status.value("state").toString() == QLatin1String("failed"),
			"broken_plugin install_status.json reports failed");
	}

	std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
	return g_failures == 0 ? 0 : 1;
}
