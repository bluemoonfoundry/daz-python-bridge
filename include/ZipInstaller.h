#pragma once

#include <QString>

// Hardened installer for drag-and-dropped plugin zip archives
// (daz-python-bridge-sop.3).
//
// Defends the filesystem against a hostile archive: path traversal
// (zip-slip), symlink entries, and zip-bomb resource exhaustion. Does NOT
// defend against a malicious plugin's actual Python code -- arbitrary code
// execution is inherent to running a third-party plugin and is a separate
// trust-model concern for community distribution, not solved by extraction
// hardening.
//
// Every install extracts into a fresh staging directory under
// <pluginsDir>/.staging/<uuid>/. The live plugins directory is only ever
// touched once by an atomic rename of that staging directory into
// <pluginsDir>/<id>/, and only after every entry has been validated AND
// fully, successfully extracted, and manifest.json has been checked. Any
// rejection or extraction failure removes the staging directory and leaves
// the live plugins dir untouched.
//
// Byte/entry-count limits are enforced twice: once cheaply from the zip
// central directory's header fields (fast rejection of obviously-bad
// archives), and again live, counted from the bytes actually written during
// decompression -- a crafted header cannot lie its way past the second
// check, which is what actually defends against zip bombs.
//
// install() refuses to touch an existing <pluginsDir>/<id> -- reinstalling
// over one is PluginInstaller's job (daz-python-bridge-sop.8), which needs to
// stop that plugin's warm worker (a live python.exe holds file locks on its
// own venv on Windows) before anything on disk can move. extractToStaging()
// and commit() are the two halves that lets PluginInstaller do that: extract
// and learn the plugin id without touching the live directory, decide what
// to do about a same-id collision, *then* commit.
class ZipInstaller {
public:
	struct Limits {
		qint64 maxTotalUncompressedBytes = 200LL * 1024 * 1024;  // whole archive
		qint64 maxEntryUncompressedBytes = 100LL * 1024 * 1024;  // any single entry
		int    maxEntryCount             = 5000;
		// Rejects entries whose claimed uncompressed:compressed ratio is
		// absurd even when both sizes are individually under the caps above
		// (the classic zip-bomb shape: a tiny compressed entry inflating to
		// something enormous).
		qint64 maxCompressionRatio       = 1024;
	};

	struct Result {
		bool    success = false;
		QString pluginId;        // resolved from manifest.json's "id" field
		QString finalPluginDir;  // <pluginsDir>/<pluginId>, valid only if success
		QString errorMessage;    // human-readable reason, valid only if !success
	};

	// Result of extractToStaging(): the archive has been validated and fully
	// extracted into a private staging directory, but <pluginsDir>/<id> has
	// not been touched. Caller must eventually either commit() stagingDir
	// into place or remove it (a failed/abandoned install must not leak
	// staging directories).
	struct StageResult {
		bool    success = false;
		QString pluginId;      // from manifest.json's "id" field
		QString version;       // from manifest.json's optional "version" field; "" if absent
		QString stagingDir;    // valid only if success
		QString errorMessage;  // valid only if !success
	};

	struct CommitResult {
		bool    success = false;
		QString finalPluginDir;
		QString errorMessage;
	};

	explicit ZipInstaller(QString pluginsDir, Limits limits = Limits());

	// Validates and extracts zipPath into a staging directory, then atomically
	// renames it into <pluginsDir>/<id> once every entry has passed and
	// manifest.json (required, must contain a string "id" field matching
	// [A-Za-z0-9_-]+) has been read from the extracted root. Fails outright
	// -- staging is discarded -- if <pluginsDir>/<id> already exists; use
	// extractToStaging()/commit() for a reinstall-aware caller.
	Result install(const QString &zipPath) const;

	// Validates and extracts zipPath into a fresh staging directory under
	// <pluginsDir>/.staging/<uuid>/ without touching <pluginsDir>/<id>.
	StageResult extractToStaging(const QString &zipPath) const;

	// Atomically swaps stagingDir into <pluginsDir>/<pluginId>, replacing
	// whatever was already there if anything (overwrite-by-id). Purely a
	// filesystem operation -- has no notion of a plugin's warm worker process;
	// the caller must ensure nothing still holds a lock under an existing
	// install (e.g. by stopping its worker first) or this will fail on
	// Windows rather than corrupt the install.
	CommitResult commit(const QString &pluginId, const QString &stagingDir) const;

private:
	QString m_pluginsDir;
	Limits  m_limits;
};
