#include "ZipInstaller.h"

#include "miniz.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

namespace {

// Zip entries always use forward slashes per the zip spec, but a malicious
// entry could smuggle a backslash to escape on Windows -- treat both as path
// separators when validating.
bool isSafeRelativePath(const QString &entryName)
{
	if (entryName.isEmpty())
		return false;

	// Absolute paths: leading slash, or a Windows drive letter (e.g. "C:\...").
	if (entryName.startsWith('/') || entryName.startsWith('\\'))
		return false;
	if (entryName.size() >= 2 && entryName.at(1) == ':')
		return false;

	const QStringList segments = entryName.split(QRegularExpression("[/\\\\]"), Qt::SkipEmptyParts);
	for (const QString &segment : segments) {
		if (segment == QLatin1String(".."))
			return false;
	}
	return true;
}

// Unix symlink detection: when an entry was written by a Unix zip tool, the
// upper 16 bits of m_external_attr hold the st_mode value, whose file-type
// nibble (S_IFMT) is S_IFLNK (0xA000) for a symlink.
bool isSymlinkEntry(const mz_zip_archive_file_stat &stat)
{
	const mz_uint32 unixMode = stat.m_external_attr >> 16;
	return (unixMode & 0xF000) == 0xA000;
}

// Threaded through mz_zip_reader_extract_to_callback: writes decompressed
// bytes to destFile while independently re-counting them against the caps,
// so a header that under-reports an entry's real size cannot smuggle a
// zip bomb past the earlier header-based pre-check.
struct LiveExtractContext {
	QFile   *destFile        = nullptr;
	qint64   entryBytesSoFar = 0;
	qint64  *totalBytesSoFar = nullptr;  // shared running total across the whole archive
	qint64   entryLimit      = 0;
	qint64   totalLimit      = 0;
	bool     limitExceeded   = false;
};

size_t liveExtractWriteCallback(void *pOpaque, mz_uint64 /*fileOfs*/, const void *pBuf, size_t n)
{
	auto *ctx = static_cast<LiveExtractContext *>(pOpaque);

	ctx->entryBytesSoFar += static_cast<qint64>(n);
	*ctx->totalBytesSoFar += static_cast<qint64>(n);
	if (ctx->entryBytesSoFar > ctx->entryLimit || *ctx->totalBytesSoFar > ctx->totalLimit) {
		ctx->limitExceeded = true;
		return 0;  // any return value != n aborts extraction immediately
	}

	const qint64 written = ctx->destFile->write(static_cast<const char *>(pBuf), static_cast<qint64>(n));
	if (written != static_cast<qint64>(n)) {
		ctx->limitExceeded = false;  // not a limit failure, but still must abort
		return 0;
	}
	return n;
}

// RAII guard so every early-return path below still calls mz_zip_reader_end().
class ZipReaderGuard {
public:
	explicit ZipReaderGuard(mz_zip_archive *zip) : m_zip(zip) {}
	~ZipReaderGuard() { mz_zip_reader_end(m_zip); }
	ZipReaderGuard(const ZipReaderGuard &) = delete;
	ZipReaderGuard &operator=(const ZipReaderGuard &) = delete;

private:
	mz_zip_archive *m_zip;
};

}  // namespace

ZipInstaller::ZipInstaller(QString pluginsDir, Limits limits)
	: m_pluginsDir(std::move(pluginsDir))
	, m_limits(limits)
{
}

ZipInstaller::Result ZipInstaller::install(const QString &zipPath) const
{
	Result result;

	if (!QFileInfo::exists(zipPath)) {
		result.errorMessage = QStringLiteral("Archive not found: %1").arg(zipPath);
		return result;
	}

	QDir pluginsDir(m_pluginsDir);
	if (!pluginsDir.mkpath(QStringLiteral("."))) {
		result.errorMessage = QStringLiteral("Could not create plugins directory: %1").arg(m_pluginsDir);
		return result;
	}

	const QString stagingRoot = pluginsDir.filePath(QStringLiteral(".staging/") + QUuid::createUuid().toString(QUuid::WithoutBraces));
	QDir stagingDir(stagingRoot);
	if (!stagingDir.mkpath(QStringLiteral("."))) {
		result.errorMessage = QStringLiteral("Could not create staging directory: %1").arg(stagingRoot);
		return result;
	}

	auto failAndCleanStaging = [&](const QString &message) -> Result {
		QDir(stagingRoot).removeRecursively();
		result.success = false;
		result.errorMessage = message;
		return result;
	};

	mz_zip_archive zip;
	memset(&zip, 0, sizeof(zip));
	if (!mz_zip_reader_init_file(&zip, zipPath.toUtf8().constData(), 0)) {
		return failAndCleanStaging(QStringLiteral("Not a valid zip archive: %1").arg(zipPath));
	}
	ZipReaderGuard zipGuard(&zip);

	const mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
	if (static_cast<int>(numFiles) > m_limits.maxEntryCount) {
		return failAndCleanStaging(QStringLiteral("Archive has too many entries (%1 > %2)")
			.arg(numFiles).arg(m_limits.maxEntryCount));
	}

	qint64 liveTotalBytes = 0;

	for (mz_uint i = 0; i < numFiles; ++i) {
		mz_zip_archive_file_stat stat;
		if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
			return failAndCleanStaging(QStringLiteral("Could not read archive entry %1").arg(i));
		}

		if (stat.m_is_encrypted) {
			return failAndCleanStaging(QStringLiteral("Encrypted entries are not allowed: %1")
				.arg(QString::fromUtf8(stat.m_filename)));
		}
		if (!stat.m_is_supported) {
			return failAndCleanStaging(QStringLiteral("Unsupported compression method for entry: %1")
				.arg(QString::fromUtf8(stat.m_filename)));
		}

		const QString entryName = QString::fromUtf8(stat.m_filename);
		if (!isSafeRelativePath(entryName)) {
			return failAndCleanStaging(QStringLiteral("Archive entry has an unsafe path: %1").arg(entryName));
		}
		if (isSymlinkEntry(stat)) {
			return failAndCleanStaging(QStringLiteral("Symlink entries are not allowed: %1").arg(entryName));
		}

		// Header-based pre-check: cheap, but a crafted header can lie -- the
		// real enforcement is the live recount in liveExtractWriteCallback below.
		if (stat.m_uncomp_size > static_cast<mz_uint64>(m_limits.maxEntryUncompressedBytes)) {
			return failAndCleanStaging(QStringLiteral("Archive entry exceeds the per-entry size limit: %1").arg(entryName));
		}
		if (stat.m_uncomp_size > 0 && stat.m_comp_size == 0) {
			return failAndCleanStaging(QStringLiteral("Archive entry has an implausible size (possible zip bomb): %1").arg(entryName));
		}
		if (stat.m_comp_size > 0) {
			const qint64 ratio = static_cast<qint64>(stat.m_uncomp_size / stat.m_comp_size);
			if (ratio > m_limits.maxCompressionRatio) {
				return failAndCleanStaging(QStringLiteral("Archive entry's compression ratio is implausible (possible zip bomb): %1").arg(entryName));
			}
		}

		if (stat.m_is_directory) {
			if (!stagingDir.mkpath(entryName)) {
				return failAndCleanStaging(QStringLiteral("Could not create directory for entry: %1").arg(entryName));
			}
			continue;
		}

		const QString destPath = stagingDir.filePath(entryName);
		const QFileInfo destInfo(destPath);
		if (!QDir().mkpath(destInfo.absolutePath())) {
			return failAndCleanStaging(QStringLiteral("Could not create parent directory for entry: %1").arg(entryName));
		}

		QFile destFile(destPath);
		if (!destFile.open(QIODevice::WriteOnly)) {
			return failAndCleanStaging(QStringLiteral("Could not write extracted entry: %1").arg(entryName));
		}

		LiveExtractContext ctx;
		ctx.destFile = &destFile;
		ctx.totalBytesSoFar = &liveTotalBytes;
		ctx.entryLimit = m_limits.maxEntryUncompressedBytes;
		ctx.totalLimit = m_limits.maxTotalUncompressedBytes;

		const mz_bool extractOk = mz_zip_reader_extract_to_callback(&zip, i, liveExtractWriteCallback, &ctx, 0);
		destFile.close();

		if (!extractOk) {
			if (ctx.limitExceeded) {
				return failAndCleanStaging(QStringLiteral("Archive exceeded size limits during extraction (zip bomb defense): %1").arg(entryName));
			}
			return failAndCleanStaging(QStringLiteral("Failed to extract archive entry: %1").arg(entryName));
		}
	}

	// manifest.json is required at the extracted root and must name the
	// plugin id this install will land under.
	const QString manifestPath = stagingDir.filePath(QStringLiteral("manifest.json"));
	QFile manifestFile(manifestPath);
	if (!manifestFile.open(QIODevice::ReadOnly)) {
		return failAndCleanStaging(QStringLiteral("Archive is missing manifest.json at its root"));
	}
	const QByteArray manifestBytes = manifestFile.readAll();
	manifestFile.close();

	QJsonParseError parseError;
	const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes, &parseError);
	if (parseError.error != QJsonParseError::NoError || !manifestDoc.isObject()) {
		return failAndCleanStaging(QStringLiteral("manifest.json is not valid JSON: %1").arg(parseError.errorString()));
	}

	const QString pluginId = manifestDoc.object().value(QStringLiteral("id")).toString();
	static const QRegularExpression idPattern(QStringLiteral("^[A-Za-z0-9_-]+$"));
	if (pluginId.isEmpty() || !idPattern.match(pluginId).hasMatch()) {
		return failAndCleanStaging(QStringLiteral("manifest.json 'id' field must be a non-empty string matching [A-Za-z0-9_-]+"));
	}

	const QString finalDir = pluginsDir.filePath(pluginId);
	if (QFileInfo::exists(finalDir)) {
		return failAndCleanStaging(QStringLiteral(
			"Plugin '%1' is already installed; reinstall is not handled by this installer").arg(pluginId));
	}

	if (!QDir().rename(stagingRoot, finalDir)) {
		return failAndCleanStaging(QStringLiteral("Could not move staged plugin into place: %1").arg(finalDir));
	}

	result.success = true;
	result.pluginId = pluginId;
	result.finalPluginDir = finalDir;
	return result;
}
