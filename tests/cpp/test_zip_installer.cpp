// Manual/CI test harness for ZipInstaller (daz-python-bridge-sop.3).
//
// Exercises the hardened extractor against real crafted archives (see
// tests/cpp_fixtures/generate_fixtures.py) rather than just inspecting code:
// a valid install, a path-traversal (zip-slip) entry, a symlink entry, an
// honestly-oversized entry, and a "lying header" zip-bomb whose declared
// uncompressed size is patched to look small while the real inflate output
// is not -- proving the live, recounted-during-decompression cap (not the
// header pre-check) is what actually stops it.
//
// Not wired into CTest (no test framework dependency in this repo yet);
// run directly: build_standalone/plugin/test_zip_installer (or wherever
// CMAKE_RUNTIME_OUTPUT_DIRECTORY places it) and check the exit code / output.

#include "ZipInstaller.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

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
	// This binary is built under <build>/.../, and the source tree layout is
	// fixed relative to this file, so resolve fixtures relative to __FILE__
	// rather than relying on the current working directory at run time.
	QFileInfo self(QString::fromUtf8(__FILE__));
	return QDir(self.absolutePath()).filePath("../cpp_fixtures");
}

}  // namespace

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	const QString fixtures = fixturesDir();

	// ─── Valid archive installs atomically into place ──────────────────────
	{
		QTemporaryDir pluginsDir;
		expectTrue(pluginsDir.isValid(), "temp plugins dir created (valid case)");

		ZipInstaller installer(pluginsDir.path());
		const ZipInstaller::Result result = installer.install(QDir(fixtures).filePath("valid_plugin.zip"));

		expectTrue(result.success, "valid archive installs successfully");
		expectTrue(result.pluginId == QLatin1String("valid_plugin"), "resolved plugin id matches manifest.json");
		expectTrue(QFileInfo::exists(QDir(pluginsDir.path()).filePath("valid_plugin/manifest.json")),
			"manifest.json landed in the final plugin dir");
		expectTrue(QFileInfo::exists(QDir(pluginsDir.path()).filePath("valid_plugin/subdir/helper.py")),
			"nested entry landed in the final plugin dir");
		expectTrue(!QFileInfo::exists(QDir(pluginsDir.path()).filePath(".staging")) ||
			QDir(QDir(pluginsDir.path()).filePath(".staging")).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty(),
			"staging dir left empty/removed after a successful install");
	}

	// ─── Path traversal (zip-slip) entry is rejected ────────────────────────
	{
		QTemporaryDir pluginsDir;
		ZipInstaller installer(pluginsDir.path());
		const ZipInstaller::Result result = installer.install(QDir(fixtures).filePath("path_traversal.zip"));

		expectTrue(!result.success, "path-traversal archive is rejected");
		expectTrue(!QFileInfo::exists(QDir(pluginsDir.path()).filePath("../evil.txt")),
			"no file was written outside the plugins dir");
		expectTrue(!QFileInfo::exists(QDir(pluginsDir.path()).filePath("evil_plugin")),
			"rejected archive did not land in the final plugins dir");
	}

	// ─── Symlink entry is rejected ──────────────────────────────────────────
	{
		QTemporaryDir pluginsDir;
		ZipInstaller installer(pluginsDir.path());
		const ZipInstaller::Result result = installer.install(QDir(fixtures).filePath("symlink_entry.zip"));

		expectTrue(!result.success, "symlink-containing archive is rejected");
		expectTrue(!QFileInfo::exists(QDir(pluginsDir.path()).filePath("evil_plugin")),
			"rejected symlink archive did not land in the final plugins dir");
	}

	// ─── Honestly-oversized entry is rejected by the header pre-check ──────
	{
		QTemporaryDir pluginsDir;
		ZipInstaller::Limits limits;
		limits.maxEntryUncompressedBytes = 1024 * 1024;  // 1 MiB cap; fixture entry is 2 MiB
		ZipInstaller installer(pluginsDir.path(), limits);
		const ZipInstaller::Result result = installer.install(QDir(fixtures).filePath("oversized_entry.zip"));

		expectTrue(!result.success, "honestly-oversized entry is rejected");
		expectTrue(!QFileInfo::exists(QDir(pluginsDir.path()).filePath("big_plugin")),
			"rejected oversized archive did not land in the final plugins dir");
	}

	// ─── Lying header: pre-check would pass, live recount must not ─────────
	{
		QTemporaryDir pluginsDir;
		ZipInstaller::Limits limits;
		limits.maxEntryUncompressedBytes = 1024 * 1024;   // 1 MiB; lied-about size (100 B) passes this
		limits.maxTotalUncompressedBytes = 1024 * 1024;
		limits.maxCompressionRatio = 1024;                 // lied ratio (100/2049 < 1) also passes this
		ZipInstaller installer(pluginsDir.path(), limits);
		const ZipInstaller::Result result = installer.install(QDir(fixtures).filePath("lying_header.zip"));

		expectTrue(!result.success, "zip bomb with a lying header is rejected by the live byte-count cap");
		expectTrue(!QFileInfo::exists(QDir(pluginsDir.path()).filePath("lying_plugin")),
			"rejected lying-header archive did not land in the final plugins dir");
	}

	// ─── Reinstall over an existing plugin id is refused (sop.8's job, not ours) ──
	{
		QTemporaryDir pluginsDir;
		ZipInstaller installer(pluginsDir.path());
		const ZipInstaller::Result first = installer.install(QDir(fixtures).filePath("valid_plugin.zip"));
		expectTrue(first.success, "first install of valid_plugin succeeds");

		const ZipInstaller::Result second = installer.install(QDir(fixtures).filePath("valid_plugin.zip"));
		expectTrue(!second.success, "reinstalling the same plugin id is refused, not silently overwritten");
	}

	std::printf("\n%s\n", g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
	return g_failures == 0 ? 0 : 1;
}
