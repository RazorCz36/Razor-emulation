#include "razorStoreDialog.h"

#include "common/archive.h"
#include "gameContent.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMap>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSize>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

namespace {

constexpr char OFFICIAL_CATALOG_URL[] =
    "https://raw.githubusercontent.com/RazorCz36/Razor-emulation/main/store/catalog.json";
constexpr char PROSPERO_HOMEBREW_CATALOG_URL[] = "https://prosperostore.net/catalog/v1.json";

constexpr char SETTINGS_STORE_GROUP[]            = "RazorStore";
constexpr char SETTINGS_CUSTOM_CATALOGS[]        = "custom_catalog_urls";
constexpr char SETTINGS_LOCAL_CATALOG_FILES[]    = "local_catalog_files";
constexpr char SETTINGS_ENABLE_PROSPERO_FEED[]   = "enable_prospero_homebrew_feed";
constexpr qint64 MAX_CATALOG_BYTES               = 32LL * 1024LL * 1024LL;
constexpr int    MAX_CONCURRENT_ICON_DOWNLOADS   = 6;

QString FormatByteSize(uint64_t bytes) {
	if (bytes == 0) {
		return QObject::tr("Auto");
	}
	if (bytes < 1024ULL) {
		return QStringLiteral("%1 B").arg(bytes);
	}
	if (bytes < 1024ULL * 1024ULL) {
		return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
	}
	if (bytes < 1024ULL * 1024ULL * 1024ULL) {
		return QStringLiteral("%1 MB")
		    .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
	}
	return QStringLiteral("%1 GB")
	    .arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

QString SanitizeFolderName(const QString& raw) {
	QString cleaned = raw.trimmed();
	cleaned.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._\\- ]")), QStringLiteral("_"));
	cleaned = cleaned.simplified();
	if (cleaned.isEmpty() || cleaned == QStringLiteral(".") || cleaned == QStringLiteral("..")) {
		return QStringLiteral("RazorStoreGame");
	}
	return cleaned;
}

QString ComputeFileSha256(const QString& path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return {};
	}
	return QString::fromLatin1(hash.result().toHex()).toLower();
}

bool IsConsoleOnlyFormat(const QString& fmt_lower) {
	return fmt_lower == QStringLiteral("pkg") || fmt_lower == QStringLiteral("fpkg") ||
	       fmt_lower == QStringLiteral("ffpkg") || fmt_lower == QStringLiteral("ffpfs") ||
	       fmt_lower == QStringLiteral("ffpfsc") || fmt_lower == QStringLiteral("exfat") ||
	       fmt_lower == QStringLiteral("pfs");
}

QString DetectFormatFromUrlOrName(const QString& url_or_name, const QString& explicit_format = {}) {
	const QString exp = explicit_format.trimmed().toLower();
	if (!exp.isEmpty()) {
		if (exp.startsWith(QLatin1Char('.'))) {
			return exp.mid(1);
		}
		return exp;
	}
	const QString clean_path = QUrl(url_or_name).path().isEmpty() ? url_or_name : QUrl(url_or_name).path();
	const QString lower      = clean_path.trimmed().toLower();
	if (lower.endsWith(QStringLiteral(".zar"))) {
		return QStringLiteral("zar");
	}
	if (lower.endsWith(QStringLiteral(".tar.gz")) || lower.endsWith(QStringLiteral(".tgz"))) {
		return QStringLiteral("tar.gz");
	}
	if (lower.endsWith(QStringLiteral(".tar"))) {
		return QStringLiteral("tar");
	}
	if (lower.contains(QStringLiteral(".7z"))) {
		return QStringLiteral("7z");
	}
	if (lower.endsWith(QStringLiteral(".zip"))) {
		return QStringLiteral("zip");
	}
	if (lower.endsWith(QStringLiteral(".elf"))) {
		return QStringLiteral("elf");
	}
	if (lower.endsWith(QStringLiteral(".bin"))) {
		return QStringLiteral("bin");
	}
	if (lower.endsWith(QStringLiteral(".ffpfsc"))) {
		return QStringLiteral("ffpfsc");
	}
	if (lower.endsWith(QStringLiteral(".ffpkg"))) {
		return QStringLiteral("ffpkg");
	}
	if (lower.endsWith(QStringLiteral(".exfat"))) {
		return QStringLiteral("exfat");
	}
	if (lower.endsWith(QStringLiteral(".pkg"))) {
		return QStringLiteral("pkg");
	}
	if (lower.contains(QStringLiteral(".rar"))) {
		return QStringLiteral("rar");
	}
	return QStringLiteral("zip");
}

QString PrettyProsperoCategory(const QString& kind, const QString& raw_category) {
	const QString k = kind.trimmed().toLower();
	const QString c = raw_category.trimmed().toLower();
	if (c == QStringLiteral("homebrew-games")) {
		return QStringLiteral("Game · Homebrew");
	}
	if (c == QStringLiteral("pc-ports")) {
		return QStringLiteral("Game · PC Port");
	}
	if (c == QStringLiteral("console-ports")) {
		return QStringLiteral("Game · Console Port");
	}
	if (c == QStringLiteral("emulators")) {
		return QStringLiteral("App · Emulator");
	}
	if (c == QStringLiteral("music") || c == QStringLiteral("media")) {
		return QStringLiteral("App · Media");
	}
	if (c == QStringLiteral("utilities")) {
		return QStringLiteral("App · Utility");
	}
	if (k == QStringLiteral("game")) {
		return QStringLiteral("Game");
	}
	if (k == QStringLiteral("dlc")) {
		return QStringLiteral("DLC");
	}
	if (k == QStringLiteral("update")) {
		return QStringLiteral("Update");
	}
	if (k == QStringLiteral("app")) {
		return QStringLiteral("Homebrew App");
	}
	return raw_category.trimmed().isEmpty() ? QStringLiteral("Game") : raw_category.trimmed();
}

QString ResolveSystemTar() {
#if defined(_WIN32)
	const QString system_root = qEnvironmentVariable("SystemRoot");
	if (!system_root.isEmpty()) {
		const QString win_tar = QDir(system_root).filePath(QStringLiteral("System32/tar.exe"));
		if (QFileInfo::exists(win_tar)) {
			return win_tar;
		}
	}
	return QStandardPaths::findExecutable(QStringLiteral("tar.exe"));
#else
	if (QFileInfo::exists(QStringLiteral("/usr/bin/tar"))) {
		return QStringLiteral("/usr/bin/tar");
	}
	if (QFileInfo::exists(QStringLiteral("/bin/tar"))) {
		return QStringLiteral("/bin/tar");
	}
	return QStandardPaths::findExecutable(QStringLiteral("tar"));
#endif
}

QString Resolve7ZipExtractor() {
	for (const QString& candidate:
	     {QStringLiteral("7zz"), QStringLiteral("7z"), QStringLiteral("7za")}) {
		const QString found = QStandardPaths::findExecutable(candidate);
		if (!found.isEmpty()) {
			return found;
		}
	}
#if defined(_WIN32)
	const QString pf = qEnvironmentVariable("ProgramFiles");
	if (!pf.isEmpty()) {
		const QString seven = QDir(pf).filePath(QStringLiteral("7-Zip/7z.exe"));
		if (QFileInfo::exists(seven)) {
			return seven;
		}
	}
#endif
	return {};
}

bool ValidatePathListSafe(const QStringList& lines, QString* out_error) {
	if (lines.isEmpty()) {
		if (out_error != nullptr) {
			*out_error = QObject::tr("Archive is empty.");
		}
		return false;
	}
	for (const QString& raw_entry: lines) {
		QString entry = raw_entry.trimmed();
		if (entry.isEmpty()) {
			continue;
		}
		entry.replace(QLatin1Char('\\'), QLatin1Char('/'));
		if (entry.startsWith(QLatin1Char('/')) ||
		    entry.contains(QRegularExpression(QStringLiteral("^[A-Za-z]:")))) {
			if (out_error != nullptr) {
				*out_error = QObject::tr("Rejected unsafe absolute path inside archive: %1")
				                 .arg(raw_entry);
			}
			return false;
		}
		const QStringList parts = entry.split(QLatin1Char('/'), Qt::SkipEmptyParts);
		for (const QString& part: parts) {
			if (part == QStringLiteral("..")) {
				if (out_error != nullptr) {
					*out_error = QObject::tr("Rejected unsafe parent-traversal entry in archive: %1")
					                 .arg(raw_entry);
				}
				return false;
			}
		}
	}
	return true;
}

bool ExtractArchiveSafely(const QString& archive_path, const QString& staging_path,
                          QString* out_error) {
	const QString lower_name = QFileInfo(archive_path).fileName().toLower();
	const QString tar_exe    = ResolveSystemTar();

	// Try system tar / bsdtar first (handles .zip, .tar.gz, .tgz, .tar, and .7z via libarchive on Win11/macOS)
	if (!tar_exe.isEmpty()) {
		QProcess list_proc;
		list_proc.start(tar_exe, {QStringLiteral("-tf"), archive_path});
		if (list_proc.waitForFinished(45000) && list_proc.exitStatus() == QProcess::NormalExit &&
		    list_proc.exitCode() == 0) {
			const QString     listing = QString::fromUtf8(list_proc.readAllStandardOutput());
			const QStringList lines =
			    listing.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
			if (!ValidatePathListSafe(lines, out_error)) {
				return false;
			}

			QProcess extract_proc;
			extract_proc.start(
			    tar_exe, {QStringLiteral("-xf"), archive_path, QStringLiteral("-C"), staging_path});
			if (extract_proc.waitForFinished(300000) &&
			    extract_proc.exitStatus() == QProcess::NormalExit && extract_proc.exitCode() == 0) {
				return true;
			}
		}
	}

	// Fallback to 7z / 7zz (especially helpful on Linux where GNU tar does not handle .zip / .7z)
	const QString seven_zip = Resolve7ZipExtractor();
	if (!seven_zip.isEmpty()) {
		QProcess list_proc;
		list_proc.start(seven_zip, {QStringLiteral("l"), QStringLiteral("-slt"),
		                            QStringLiteral("-ba"), archive_path});
		if (list_proc.waitForFinished(45000) && list_proc.exitStatus() == QProcess::NormalExit &&
		    list_proc.exitCode() == 0) {
			const QString     raw_out = QString::fromUtf8(list_proc.readAllStandardOutput());
			QStringList       paths;
			const QStringList lines =
			    raw_out.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
			for (const QString& line: lines) {
				if (line.startsWith(QStringLiteral("Path = "))) {
					paths.push_back(line.mid(7));
				}
			}
			if (!paths.isEmpty() && !ValidatePathListSafe(paths, out_error)) {
				return false;
			}

			QProcess extract_proc;
			extract_proc.start(seven_zip, {QStringLiteral("x"), QStringLiteral("-y"),
			                               QStringLiteral("-o") + staging_path, archive_path});
			if (extract_proc.waitForFinished(300000) &&
			    extract_proc.exitStatus() == QProcess::NormalExit && extract_proc.exitCode() == 0) {
				return true;
			}
		}
	}

#if !defined(_WIN32)
	// Fallback to POSIX unzip for .zip on minimal Linux installs where tar is GNU tar and 7z isn't installed
	if (lower_name.endsWith(QStringLiteral(".zip"))) {
		const QString unzip_exe = QStandardPaths::findExecutable(QStringLiteral("unzip"));
		if (!unzip_exe.isEmpty()) {
			QProcess list_proc;
			list_proc.start(unzip_exe, {QStringLiteral("-Z1"), archive_path});
			if (list_proc.waitForFinished(30000) &&
			    list_proc.exitStatus() == QProcess::NormalExit && list_proc.exitCode() == 0) {
				const QString     listing = QString::fromUtf8(list_proc.readAllStandardOutput());
				const QStringList lines   = listing.split(
                    QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
				if (!ValidatePathListSafe(lines, out_error)) {
					return false;
				}
				QProcess extract_proc;
				extract_proc.start(unzip_exe, {QStringLiteral("-oq"), archive_path,
				                               QStringLiteral("-d"), staging_path});
				if (extract_proc.waitForFinished(300000) &&
				    extract_proc.exitStatus() == QProcess::NormalExit &&
				    extract_proc.exitCode() == 0) {
					return true;
				}
			}
		}
	}
#endif

	if (out_error != nullptr && out_error->isEmpty()) {
		*out_error = QObject::tr(
		    "Archive extraction failed (corrupt archive, HTML verification page instead of binary file, "
		    "or missing extractor for %1).")
		                 .arg(QFileInfo(archive_path).fileName());
	}
	return false;
}

bool LooksLikeHtmlPage(const QString& file_path) {
	QFile f(file_path);
	if (!f.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray head = f.read(1024).trimmed().toLower();
	return head.startsWith("<!doctype html") || head.startsWith("<html") ||
	       head.contains("<head>") || head.contains("cloudflare") || head.contains("captcha");
}

QString ReadTitleIdFromParamJson(const QByteArray& param_bytes) {
	const QJsonDocument doc = QJsonDocument::fromJson(param_bytes);
	if (!doc.isObject()) {
		return {};
	}
	const QJsonObject root = doc.object();
	QString           tid  = root.value(QStringLiteral("titleId")).toString().trimmed().toUpper();
	if (!tid.isEmpty()) {
		return tid;
	}
	// PS5 SDK param.json often stores the Title ID inside contentId ("ED5843-PPSA28319_00-...")
	const QString content_id = root.value(QStringLiteral("contentId")).toString().trimmed().toUpper();
	if (content_id.size() >= 17 && content_id[6] == QLatin1Char('-') &&
	    content_id[16] == QLatin1Char('_')) {
		return content_id.mid(7, 9);
	}
	static const QRegularExpression ppsa_re(QStringLiteral("([A-Z]{4}[0-9]{5})"));
	const auto                      match = ppsa_re.match(content_id);
	if (match.hasMatch()) {
		return match.captured(1);
	}
	return {};
}

bool EnsureMinimalParamJson(const QString& game_root_dir, const QString& fallback_title_id,
                            const QString& fallback_name, QString* resolved_title_id) {
	const QString sce_sys_dir = QDir(game_root_dir).filePath(QStringLiteral("sce_sys"));
	const QString param_path  = QDir(sce_sys_dir).filePath(QStringLiteral("param.json"));

	if (QFileInfo::exists(param_path)) {
		QFile existing(param_path);
		if (existing.open(QIODevice::ReadOnly)) {
			const QString tid = ReadTitleIdFromParamJson(existing.readAll());
			if (resolved_title_id != nullptr && !tid.isEmpty()) {
				*resolved_title_id = tid;
			}
		}
		return true;
	}

	if (!QDir().mkpath(sce_sys_dir)) {
		return false;
	}

	const QString tid = !fallback_title_id.trimmed().isEmpty()
	                        ? fallback_title_id.trimmed().toUpper()
	                        : QStringLiteral("PPSA99999");
	const QString title_name = !fallback_name.trimmed().isEmpty()
	                               ? fallback_name.trimmed()
	                               : QFileInfo(game_root_dir).fileName();

	QJsonObject en_us;
	en_us.insert(QStringLiteral("titleName"), title_name);

	QJsonObject localized;
	localized.insert(QStringLiteral("defaultLanguage"), QStringLiteral("en-US"));
	localized.insert(QStringLiteral("en-US"), en_us);

	QJsonObject root;
	root.insert(QStringLiteral("titleId"), tid);
	root.insert(QStringLiteral("contentVersion"), QStringLiteral("01.00"));
	root.insert(QStringLiteral("masterVersion"), QStringLiteral("01.00"));
	root.insert(QStringLiteral("localizedParameters"), localized);

	QSaveFile out(param_path);
	if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (!out.commit()) {
		return false;
	}
	if (resolved_title_id != nullptr) {
		*resolved_title_id = tid;
	}
	return true;
}

QPixmap MakeFallbackCoverPixmap(const QString& title, const QString& title_id, int size_px) {
	QPixmap pix(size_px, size_px);
	pix.fill(Qt::transparent);

	QPainter p(&pix);
	p.setRenderHint(QPainter::Antialiasing, true);

	QPainterPath clip;
	clip.addRoundedRect(QRectF(1.0, 1.0, size_px - 2.0, size_px - 2.0), 12.0, 12.0);
	p.setClipPath(clip);

	QLinearGradient bg(0, 0, size_px, size_px);
	bg.setColorAt(0.0, QColor(0x23, 0x28, 0x33));
	bg.setColorAt(1.0, QColor(0x12, 0x15, 0x1B));
	p.fillRect(pix.rect(), bg);

	p.setPen(QPen(QColor(0xD6, 0xB2, 0x6C), 2.0));
	p.drawRoundedRect(QRectF(1.5, 1.5, size_px - 3.0, size_px - 3.0), 11.0, 11.0);

	QFont badge_font = p.font();
	badge_font.setBold(true);
	badge_font.setPixelSize(qMax(10, size_px / 9));
	p.setFont(badge_font);
	p.setPen(QColor(0xD6, 0xB2, 0x6C));
	p.drawText(QRect(8, 8, size_px - 16, size_px / 4), Qt::AlignCenter,
	           title_id.isEmpty() ? QStringLiteral("PS5") : title_id.left(9));

	QFont monogram_font = p.font();
	monogram_font.setBold(true);
	monogram_font.setPixelSize(qMax(16, size_px / 3));
	p.setFont(monogram_font);
	p.setPen(QColor(0xEE, 0xF2, 0xF8));
	const QString initials = title.trimmed().isEmpty() ? QStringLiteral("RZ")
	                                                   : title.trimmed().left(2).toUpper();
	p.drawText(pix.rect(), Qt::AlignCenter, initials);

	return pix;
}

void ParseTitleVersionBracket(const QString& raw_title, QString* clean_title,
                              QString* extracted_version) {
	static const QRegularExpression ver_re(
	    QStringLiteral("\\s*\\((v?[0-9]+(?:\\.[0-9A-Za-z_\\-]+)+)\\)\\s*$"),
	    QRegularExpression::CaseInsensitiveOption);
	const auto match = ver_re.match(raw_title.trimmed());
	if (match.hasMatch()) {
		if (clean_title != nullptr) {
			*clean_title = raw_title.trimmed().left(match.capturedStart()).trimmed();
		}
		if (extracted_version != nullptr && extracted_version->isEmpty()) {
			QString v = match.captured(1).trimmed();
			if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
				v = v.mid(1);
			}
			*extracted_version = v;
		}
	} else if (clean_title != nullptr) {
		*clean_title = raw_title.trimmed();
	}
}

void SplitCatalogSpecAndBearer(const QString& spec, QString* out_url, QString* out_bearer) {
	const int pipe_pos = spec.indexOf(QLatin1Char('|'));
	if (pipe_pos >= 0) {
		*out_url    = spec.left(pipe_pos).trimmed();
		*out_bearer = spec.mid(pipe_pos + 1).trimmed();
	} else {
		*out_url    = spec.trimmed();
		*out_bearer = QString();
	}
}

} // namespace

namespace RazorStore {

QString LocateBundledStoreDir() {
	const QString app_dir = QCoreApplication::applicationDirPath();
	const QStringList candidates = {
	    QDir(app_dir).filePath(QStringLiteral("store")),
	    QDir(app_dir).filePath(QStringLiteral("../store")),
	    QDir(app_dir).filePath(QStringLiteral("../Resources/store")),
	    QDir(app_dir).filePath(QStringLiteral("../../store")),
	    QDir::current().filePath(QStringLiteral("store")),
	};
	for (const QString& candidate: candidates) {
		const QString clean = QDir::cleanPath(candidate);
		if (QFileInfo::exists(QDir(clean).filePath(QStringLiteral("catalog.json")))) {
			return clean;
		}
	}
	return {};
}

uint64_t ParseSizeFieldToBytes(const QString& size_text) {
	const QString trimmed = size_text.trimmed();
	if (trimmed.isEmpty()) {
		return 0;
	}
	bool           pure_int = false;
	const uint64_t raw_val  = trimmed.toULongLong(&pure_int);
	if (pure_int) {
		return raw_val;
	}

	static const QRegularExpression size_re(
	    QStringLiteral("^([0-9]+(?:[.,][0-9]+)?)\\s*(B|KB|KIB|MB|MIB|GB|GIB|TB|TIB)?$"),
	    QRegularExpression::CaseInsensitiveOption);
	const auto m = size_re.match(trimmed);
	if (!m.hasMatch()) {
		return 0;
	}
	QString num_str = m.captured(1);
	num_str.replace(QLatin1Char(','), QLatin1Char('.'));
	bool         ok  = false;
	const double val = num_str.toDouble(&ok);
	if (!ok || val <= 0.0) {
		return 0;
	}
	const QString unit = m.captured(2).toUpper();
	double        mult = 1.0;
	if (unit == QStringLiteral("KB") || unit == QStringLiteral("KIB")) {
		mult = 1024.0;
	} else if (unit == QStringLiteral("MB") || unit == QStringLiteral("MIB")) {
		mult = 1024.0 * 1024.0;
	} else if (unit == QStringLiteral("GB") || unit == QStringLiteral("GIB")) {
		mult = 1024.0 * 1024.0 * 1024.0;
	} else if (unit == QStringLiteral("TB") || unit == QStringLiteral("TIB")) {
		mult = 1024.0 * 1024.0 * 1024.0 * 1024.0;
	}
	return static_cast<uint64_t>(std::llround(val * mult));
}

static uint64_t ParseJsonSizeValue(const QJsonValue& val) {
	if (val.isDouble()) {
		const double d = val.toDouble(0.0);
		return d > 0.0 ? static_cast<uint64_t>(d) : 0ULL;
	}
	if (val.isString()) {
		return ParseSizeFieldToBytes(val.toString());
	}
	return 0ULL;
}

bool ParseCatalogJson(const QByteArray& json_bytes, const QString& base_dir,
                      QString* out_store_name, QVector<RazorStoreItem>* out_items,
                      QString* out_error, QStringList* out_nested_source_urls) {
	QJsonParseError     parse_err {};
	const QJsonDocument doc = QJsonDocument::fromJson(json_bytes, &parse_err);
	if (parse_err.error != QJsonParseError::NoError || (!doc.isObject() && !doc.isArray())) {
		if (out_error != nullptr) {
			*out_error = QObject::tr("Invalid catalog JSON: %1").arg(parse_err.errorString());
		}
		return false;
	}

	const QJsonObject root = doc.isObject() ? doc.object() : QJsonObject {};

	// Pegasus DL source list ("sources": ["https://...", {"url": "https://...", "enabled": true}])
	if (root.contains(QStringLiteral("sources")) && root.value(QStringLiteral("sources")).isArray() &&
	    !root.contains(QStringLiteral("items")) && !root.contains(QStringLiteral("entries")) &&
	    !root.contains(QStringLiteral("apps"))) {
		if (out_store_name != nullptr) {
			*out_store_name = root.value(QStringLiteral("name"))
			                      .toString(QObject::tr("Catalog Source List"))
			                      .trimmed();
		}
		if (out_nested_source_urls != nullptr) {
			const QJsonArray sources = root.value(QStringLiteral("sources")).toArray();
			for (const QJsonValue& sv: sources) {
				if (sv.isString()) {
					const QString u = sv.toString().trimmed();
					if (!u.isEmpty()) {
						out_nested_source_urls->push_back(u);
					}
				} else if (sv.isObject()) {
					const QJsonObject so      = sv.toObject();
					const bool        enabled = so.value(QStringLiteral("enabled")).toBool(true);
					const QString     u       = so.value(QStringLiteral("url")).toString().trimmed();
					if (enabled && !u.isEmpty()) {
						out_nested_source_urls->push_back(u);
					}
				}
			}
		}
		if (out_items != nullptr) {
			out_items->clear();
		}
		return true;
	}

	QVector<RazorStoreItem> parsed;

	// =========================================================================
	// Schema 1: Native Razor Store ("items": [...])
	// =========================================================================
	if (root.contains(QStringLiteral("items")) && root.value(QStringLiteral("items")).isArray()) {
		const QString store_name = root.value(QStringLiteral("store_name"))
		                               .toString(QObject::tr("Razor Store Official"))
		                               .trimmed();
		if (out_store_name != nullptr) {
			*out_store_name = store_name;
		}
		const QJsonArray items_arr = root.value(QStringLiteral("items")).toArray();
		parsed.reserve(items_arr.size());

		for (const QJsonValue& val: items_arr) {
			if (!val.isObject()) {
				continue;
			}
			const QJsonObject obj = val.toObject();
			RazorStoreItem    item;
			item.title_id     = obj.value(QStringLiteral("title_id")).toString().trimmed().toUpper();
			item.entry_id     = obj.value(QStringLiteral("id")).toString(item.title_id).trimmed();
			item.name         = obj.value(QStringLiteral("name")).toString().trimmed();
			item.subtitle     = obj.value(QStringLiteral("subtitle")).toString().trimmed();
			item.category     = obj.value(QStringLiteral("category")).toString(QStringLiteral("Game")).trimmed();
			item.version      = obj.value(QStringLiteral("version")).toString(QStringLiteral("01.00")).trimmed();
			item.author       = obj.value(QStringLiteral("author")).toString(QStringLiteral("Community")).trimmed();
			item.license      = obj.value(QStringLiteral("license")).toString(QStringLiteral("Open Homebrew")).trimmed();
			item.resolutions  = obj.value(QStringLiteral("resolutions"))
			                        .toString(QStringLiteral("1080p FHD · 1440p WQHD · 4K UHD"))
			                        .trimmed();
			item.controls     = obj.value(QStringLiteral("controls"))
			                        .toString(QStringLiteral("DualSense Controller or Mapped PC Keyboard"))
			                        .trimmed();
			item.description  = obj.value(QStringLiteral("description")).toString().trimmed();
			item.download_url = obj.value(QStringLiteral("download_url")).toString().trimmed();
			item.format       = DetectFormatFromUrlOrName(
                item.download_url, obj.value(QStringLiteral("format")).toString());
			item.size_bytes   = ParseJsonSizeValue(obj.value(QStringLiteral("size_bytes")));
			item.sha256       = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
			item.icon_url     = obj.value(QStringLiteral("icon_url")).toString().trimmed();
			item.source_name  = store_name;
			item.source_kind  = QStringLiteral("razor");
			item.console_only_format = IsConsoleOnlyFormat(item.format);

			const QString rel_pkg  = obj.value(QStringLiteral("local_fallback")).toString().trimmed();
			const QString rel_icon = obj.value(QStringLiteral("local_icon")).toString().trimmed();
			if (!base_dir.isEmpty() && !rel_pkg.isEmpty()) {
				const QString candidate = QDir::cleanPath(QDir(base_dir).filePath(rel_pkg));
				if (QFileInfo::exists(candidate)) {
					item.local_fallback_path = candidate;
				}
			}
			if (!base_dir.isEmpty() && !rel_icon.isEmpty()) {
				const QString candidate = QDir::cleanPath(QDir(base_dir).filePath(rel_icon));
				if (QFileInfo::exists(candidate)) {
					item.local_icon_path = candidate;
				}
			}

			if (item.name.isEmpty() ||
			    (item.download_url.isEmpty() && item.local_fallback_path.isEmpty())) {
				continue;
			}
			if (!item.download_url.isEmpty()) {
				item.part_urls.push_back(item.download_url);
			}
			if (item.entry_id.isEmpty()) {
				item.entry_id = item.title_id.isEmpty() ? item.name : item.title_id;
			}
			parsed.push_back(std::move(item));
		}

		if (out_items != nullptr) {
			*out_items = std::move(parsed);
		}
		return true;
	}

	// =========================================================================
	// Schema 2: darkxex/ps5-homebrew-catalog / ProsperoStore ("apps": [...])
	// (The default 212-title PS5 Homebrew Catalog in deox1111/ps5-library)
	// =========================================================================
	if (root.contains(QStringLiteral("apps")) && root.value(QStringLiteral("apps")).isArray()) {
		const QString store_name = root.value(QStringLiteral("name"))
		                               .toString(QObject::tr("PS5 Homebrew Catalog (ps5-library)"))
		                               .trimmed();
		if (out_store_name != nullptr) {
			*out_store_name = store_name;
		}

		auto parse_app_array = [&](const QJsonArray& arr) {
			for (const QJsonValue& val: arr) {
				if (!val.isObject()) {
					continue;
				}
				const QJsonObject obj    = val.toObject();
				const QString     status = obj.value(QStringLiteral("status"))
				                               .toString(QStringLiteral("available"))
				                               .trimmed()
				                               .toLower();
				if (status == QStringLiteral("coming_soon") ||
				    status == QStringLiteral("withdrawn")) {
					continue;
				}

				RazorStoreItem item;
				item.title_id = obj.value(QStringLiteral("titleid"))
				                    .toString(obj.value(QStringLiteral("title_id")).toString())
				                    .trimmed()
				                    .toUpper();
				const QString app_id =
				    obj.value(QStringLiteral("id")).toString(item.title_id).trimmed();
				item.entry_id = QStringLiteral("prospero:") + (app_id.isEmpty() ? item.title_id : app_id);
				item.name     = obj.value(QStringLiteral("name")).toString().trimmed();

				const QString kind     = obj.value(QStringLiteral("kind")).toString(QStringLiteral("game"));
				const QString raw_cat  = obj.value(QStringLiteral("category")).toString();
				item.category          = PrettyProsperoCategory(kind, raw_cat);
				item.author            = obj.value(QStringLiteral("author"))
				                             .toString(obj.value(QStringLiteral("owner")).toString(QStringLiteral("PS5 Homebrew")))
				                             .trimmed();
				item.version           = obj.value(QStringLiteral("version"))
				                             .toString(obj.value(QStringLiteral("content_version")).toString(QStringLiteral("1.0")))
				                             .trimmed();
				if (item.version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) &&
				    item.version.size() > 1) {
					item.version = item.version.mid(1);
				}
				item.license      = obj.value(QStringLiteral("license")).toString(QStringLiteral("Open Source")).trimmed();
				item.description  = obj.value(QStringLiteral("description")).toString().trimmed();
				item.download_url = obj.value(QStringLiteral("artifact_url")).toString().trimmed();
				item.format       = DetectFormatFromUrlOrName(
                    item.download_url, obj.value(QStringLiteral("format")).toString(QStringLiteral("zip")));
				item.size_bytes   = ParseJsonSizeValue(obj.value(QStringLiteral("size")));
				item.sha256       = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
				item.resolutions  = QStringLiteral("Native PS5 Prospero Package");
				item.controls     = QStringLiteral("DualSense Controller or Mapped PC Keyboard");
				item.source_name  = store_name;
				item.source_kind  = QStringLiteral("prospero");
				item.console_only_format = IsConsoleOnlyFormat(item.format);

				// Skip console-only filesystem images in the public homebrew feed so every listed
				// entry can be cleanly unpacked into Games/<TitleID>/ on PC:
				if (item.console_only_format) {
					continue;
				}

				const QString repo_url = obj.value(QStringLiteral("source_repo")).toString().trimmed();
				item.subtitle = !repo_url.isEmpty()
				                    ? QStringLiteral("%1  ·  by %2  ·  %3")
				                          .arg(item.category, item.author, repo_url)
				                    : QStringLiteral("%1  ·  by %2").arg(item.category, item.author);

				// Prefer pure PNG icon endpoint over .webp so standard Qt6 builds render covers natively:
				const QString icon_small = obj.value(QStringLiteral("icon_small")).toString().trimmed();
				const QString raw_icon   = obj.value(QStringLiteral("icon_url")).toString().trimmed();
				if (!icon_small.isEmpty()) {
					item.icon_url = icon_small;
				} else if (!app_id.isEmpty()) {
					item.icon_url =
					    QStringLiteral("https://prosperostore.net/api/v1/icons/%1-256.png").arg(app_id);
				} else if (!raw_icon.endsWith(QStringLiteral(".webp"), Qt::CaseInsensitive)) {
					item.icon_url = raw_icon;
				}

				if (item.name.isEmpty()) {
					continue;
				}
				// If parsed from api/v1/index.json (where artifact_url is fetched per-app on demand),
				// store the per-app API endpoint marker so InstallSelectedItem can resolve it seamlessly:
				if (item.download_url.isEmpty() && !app_id.isEmpty()) {
					item.download_url =
					    QStringLiteral("prospero-api://apps/%1.json").arg(app_id);
				}
				if (!item.download_url.isEmpty()) {
					item.part_urls.push_back(item.download_url);
				}
				parsed.push_back(std::move(item));
			}
		};

		parse_app_array(root.value(QStringLiteral("apps")).toArray());
		if (root.value(QStringLiteral("shared")).isArray()) {
			parse_app_array(root.value(QStringLiteral("shared")).toArray());
		}

		if (out_items != nullptr) {
			*out_items = std::move(parsed);
		}
		return true;
	}

	// =========================================================================
	// Schema 3: deox1111/ps5-library Catalog ("entries": [...])
	// =========================================================================
	if (root.contains(QStringLiteral("entries")) && root.value(QStringLiteral("entries")).isArray()) {
		const QString store_name = root.value(QStringLiteral("name"))
		                               .toString(root.value(QStringLiteral("title"))
		                                             .toString(QObject::tr("PS5 Library Custom Catalog")))
		                               .trimmed();
		if (out_store_name != nullptr) {
			*out_store_name = store_name;
		}

		const QJsonArray entries = root.value(QStringLiteral("entries")).toArray();
		parsed.reserve(entries.size());
		int seq = 0;

		for (const QJsonValue& val: entries) {
			if (!val.isObject()) {
				continue;
			}
			++seq;
			const QJsonObject obj      = val.toObject();
			const QString     platform = obj.value(QStringLiteral("platform"))
			                                 .toString(QStringLiteral("ps5"))
			                                 .trimmed()
			                                 .toLower();
			if (platform == QStringLiteral("ps4")) {
				continue; // PS4 packages are not PS5 Prospero binaries
			}

			// Extract primary files[] list (or first source_sets[0].files[])
			QJsonArray files_arr = obj.value(QStringLiteral("files")).toArray();
			QString    host_name;
			if (files_arr.isEmpty() && obj.value(QStringLiteral("source_sets")).isArray()) {
				const QJsonArray ssets = obj.value(QStringLiteral("source_sets")).toArray();
				if (!ssets.isEmpty() && ssets.first().isObject()) {
					const QJsonObject first_set = ssets.first().toObject();
					files_arr                   = first_set.value(QStringLiteral("files")).toArray();
					host_name                   = first_set.value(QStringLiteral("host")).toString().trimmed();
				}
			}

			RazorStoreItem item;
			for (const QJsonValue& fv: files_arr) {
				if (fv.isString()) {
					const QString u = fv.toString().trimmed();
					if (!u.isEmpty()) {
						item.part_urls.push_back(u);
						item.part_names.push_back(QFileInfo(QUrl(u).path()).fileName());
					}
				} else if (fv.isObject()) {
					const QJsonObject fo = fv.toObject();
					const QString     u  = fo.value(QStringLiteral("url")).toString().trimmed();
					if (u.isEmpty()) {
						continue;
					}
					QString fname = fo.value(QStringLiteral("name")).toString().trimmed();
					if (fname.isEmpty()) {
						fname = QFileInfo(QUrl(u).path()).fileName();
					}
					if (item.sha256.isEmpty()) {
						item.sha256 = fo.value(QStringLiteral("sha256")).toString().trimmed().toLower();
					}
					item.part_urls.push_back(u);
					item.part_names.push_back(fname);
				}
			}

			if (item.part_urls.isEmpty()) {
				const QString single_url = obj.value(QStringLiteral("url")).toString().trimmed();
				if (!single_url.isEmpty()) {
					item.part_urls.push_back(single_url);
					item.part_names.push_back(QFileInfo(QUrl(single_url).path()).fileName());
				}
			}
			if (item.part_urls.isEmpty()) {
				continue;
			}

			item.download_url = item.part_urls.first();
			// Resolve relative paths when loaded from a local .json catalog file:
			if (!base_dir.isEmpty() && QUrl(item.download_url).scheme().isEmpty()) {
				const QString local_candidate =
				    QDir::cleanPath(QDir(base_dir).filePath(item.download_url));
				if (QFileInfo::exists(local_candidate)) {
					item.local_fallback_path = local_candidate;
				}
			}

			item.title_id = obj.value(QStringLiteral("title_id"))
			                    .toString(obj.value(QStringLiteral("titleid")).toString())
			                    .trimmed()
			                    .toUpper();
			item.version  = obj.value(QStringLiteral("version")).toString().trimmed();

			const QString raw_title =
			    obj.value(QStringLiteral("title"))
			        .toString(obj.value(QStringLiteral("name"))
			                      .toString(!item.part_names.isEmpty() ? item.part_names.first()
			                                                           : item.title_id));
			ParseTitleVersionBracket(raw_title, &item.name, &item.version);
			if (item.version.isEmpty()) {
				item.version = QStringLiteral("01.00");
			}

			const QString custom_id = obj.value(QStringLiteral("id")).toString().trimmed();
			item.entry_id           = !custom_id.isEmpty()
			                              ? QStringLiteral("ps5lib:") + custom_id
			                              : QStringLiteral("ps5lib:%1:%2")
			                                    .arg(item.title_id.isEmpty() ? item.name : item.title_id)
			                                    .arg(seq);

			const QString kind     = obj.value(QStringLiteral("kind")).toString(QStringLiteral("game"));
			const QString region   = obj.value(QStringLiteral("region")).toString().trimmed();
			const QString firmware = obj.value(QStringLiteral("firmware")).toString().trimmed();

			item.category   = PrettyProsperoCategory(kind, obj.value(QStringLiteral("category")).toString());
			item.author     = !host_name.isEmpty()
			                      ? host_name
			                      : (QUrl(item.download_url).host().isEmpty()
			                             ? store_name
			                             : QUrl(item.download_url).host());
			item.license    = QStringLiteral("PS5 Library Feed");
			item.size_bytes = ParseJsonSizeValue(obj.value(QStringLiteral("size")));
			item.format     = DetectFormatFromUrlOrName(
                !item.part_names.isEmpty() && !item.part_names.first().isEmpty()
                    ? item.part_names.first()
                    : item.download_url,
                obj.value(QStringLiteral("format")).toString());
			item.console_only_format = IsConsoleOnlyFormat(item.format);
			item.icon_url            = obj.value(QStringLiteral("cover"))
			                    .toString(obj.value(QStringLiteral("icon")).toString())
			                    .trimmed();
			item.description = obj.value(QStringLiteral("description")).toString().trimmed();

			QStringList sub_parts {store_name};
			if (!region.isEmpty()) {
				sub_parts.push_back(QObject::tr("Region: %1").arg(region));
			}
			if (!firmware.isEmpty()) {
				sub_parts.push_back(QObject::tr("FW %1").arg(firmware));
			}
			if (item.part_urls.size() > 1) {
				sub_parts.push_back(QObject::tr("%1 parts").arg(item.part_urls.size()));
			}
			item.subtitle    = sub_parts.join(QStringLiteral("  ·  "));
			item.resolutions = item.console_only_format
			                       ? QObject::tr("Console Image (.%1) — Unpack folder for PC")
			                             .arg(item.format)
			                       : QStringLiteral("PS5 Prospero (1080p · 1440p · 4K)");
			item.controls    = QStringLiteral("DualSense Controller or Mapped PC Keyboard");
			item.source_name = store_name;
			item.source_kind = QStringLiteral("custom");

			if (item.description.isEmpty()) {
				item.description = QObject::tr("Imported from %1 (%2). Format: .%3.")
				                       .arg(store_name,
				                            item.title_id.isEmpty() ? QStringLiteral("PS5")
				                                                    : item.title_id,
				                            item.format.toUpper());
			}

			parsed.push_back(std::move(item));
		}

		if (out_items != nullptr) {
			*out_items = std::move(parsed);
		}
		return true;
	}

	// =========================================================================
	// Schema 4: Pegasus DL Catalog ("packages" / "games" / "data" or root JSON array)
	// =========================================================================
	QJsonArray pegasus_arr;
	if (doc.isArray()) {
		pegasus_arr = doc.array();
	} else if (root.value(QStringLiteral("packages")).isArray()) {
		pegasus_arr = root.value(QStringLiteral("packages")).toArray();
	} else if (root.value(QStringLiteral("games")).isArray()) {
		pegasus_arr = root.value(QStringLiteral("games")).toArray();
	} else if (root.value(QStringLiteral("data")).isArray()) {
		pegasus_arr = root.value(QStringLiteral("data")).toArray();
	}

	if (!pegasus_arr.isEmpty()) {
		const QString store_name = root.value(QStringLiteral("name"))
		                               .toString(QObject::tr("Pegasus DL Catalog"))
		                               .trimmed();
		if (out_store_name != nullptr) {
			*out_store_name = store_name;
		}
		int seq = 0;
		for (const QJsonValue& val: pegasus_arr) {
			if (!val.isObject()) {
				continue;
			}
			++seq;
			const QJsonObject obj = val.toObject();
			RazorStoreItem    item;

			if (obj.value(QStringLiteral("downloadLinks")).isArray()) {
				for (const QJsonValue& lv: obj.value(QStringLiteral("downloadLinks")).toArray()) {
					if (lv.isString()) {
						item.part_urls.push_back(lv.toString().trimmed());
						item.part_names.push_back(QFileInfo(QUrl(lv.toString()).path()).fileName());
					} else if (lv.isObject()) {
						const QJsonObject lo = lv.toObject();
						const QString     u =
						    lo.value(QStringLiteral("url"))
						        .toString(lo.value(QStringLiteral("link")).toString())
						        .trimmed();
						if (!u.isEmpty()) {
							item.part_urls.push_back(u);
							item.part_names.push_back(
							    lo.value(QStringLiteral("filename"))
							        .toString(lo.value(QStringLiteral("name"))
							                      .toString(QFileInfo(QUrl(u).path()).fileName())));
						}
					}
				}
			} else {
				const QString u = obj.value(QStringLiteral("url")).toString().trimmed();
				if (!u.isEmpty()) {
					item.part_urls.push_back(u);
					item.part_names.push_back(
					    obj.value(QStringLiteral("filename"))
					        .toString(QFileInfo(QUrl(u).path()).fileName()));
				}
			}

			if (item.part_urls.isEmpty()) {
				continue;
			}
			item.download_url = item.part_urls.first();
			item.title_id     = obj.value(QStringLiteral("titleId"))
			                    .toString(obj.value(QStringLiteral("title_id"))
			                                  .toString(obj.value(QStringLiteral("cusa")).toString()))
			                    .trimmed()
			                    .toUpper();
			if (item.title_id.startsWith(QStringLiteral("CUSA"))) {
				continue; // Skip PS4 entries
			}
			item.version = obj.value(QStringLiteral("version")).toString(QStringLiteral("01.00")).trimmed();
			ParseTitleVersionBracket(
			    obj.value(QStringLiteral("name")).toString(obj.value(QStringLiteral("title")).toString()),
			    &item.name, &item.version);
			if (item.name.isEmpty()) {
				continue;
			}
			item.entry_id   = QStringLiteral("pegasus:%1:%2").arg(item.title_id, QString::number(seq));
			item.category   = QStringLiteral("Game");
			item.author     = store_name;
			item.license    = QStringLiteral("Pegasus Catalog");
			item.size_bytes = ParseJsonSizeValue(obj.value(QStringLiteral("size")));
			item.format     = DetectFormatFromUrlOrName(
                !item.part_names.isEmpty() ? item.part_names.first() : item.download_url,
                obj.value(QStringLiteral("format")).toString());
			item.console_only_format = IsConsoleOnlyFormat(item.format);
			item.icon_url            = obj.value(QStringLiteral("icon"))
			                    .toString(obj.value(QStringLiteral("cover")).toString())
			                    .trimmed();
			item.subtitle    = store_name;
			item.resolutions = QStringLiteral("PS5 Prospero (1080p · 1440p · 4K)");
			item.controls    = QStringLiteral("DualSense Controller or Mapped PC Keyboard");
			item.description = obj.value(QStringLiteral("description"))
			                       .toString(QObject::tr("Pegasus DL package (%1).").arg(item.name));
			item.source_name = store_name;
			item.source_kind = QStringLiteral("custom");
			parsed.push_back(std::move(item));
		}

		if (out_items != nullptr) {
			*out_items = std::move(parsed);
		}
		return true;
	}

	if (out_error != nullptr) {
		*out_error = QObject::tr(
		    "Unrecognized catalog schema (expected 'items', 'entries' [ps5-library], 'apps' [ProsperoStore], or 'packages' [Pegasus DL]).");
	}
	return false;
}

RazorStoreInstallResult InstallPackageToDirectory(const QString& source_package_path,
                                                  const QString& target_games_dir,
                                                  const QString& preferred_title_id,
                                                  const QString& preferred_name,
                                                  const QString& expected_sha256) {
	RazorStoreInstallResult result;

	const QFileInfo src_info(source_package_path);
	if (!src_info.exists() || !src_info.isFile()) {
		result.error_message = QObject::tr("Package file does not exist: %1").arg(source_package_path);
		return result;
	}

	if (!QDir().mkpath(target_games_dir)) {
		result.error_message =
		    QObject::tr("Could not create target Games directory: %1").arg(target_games_dir);
		return result;
	}

	if (LooksLikeHtmlPage(source_package_path)) {
		result.error_message = QObject::tr(
		    "The server returned an HTML web page (CAPTCHA / browser verification) instead of a binary PS5 archive.");
		return result;
	}

	if (!expected_sha256.trimmed().isEmpty()) {
		const QString actual_sha = ComputeFileSha256(source_package_path);
		if (actual_sha.compare(expected_sha256.trimmed(), Qt::CaseInsensitive) != 0) {
			result.error_message = QObject::tr("SHA-256 verification failed.\nExpected: %1\nActual: %2")
			                           .arg(expected_sha256.trimmed().toLower(), actual_sha);
			return result;
		}
	}

	const QString lower_name = src_info.fileName().toLower();

	// Reject console-only encrypted / ShadowMountPlus filesystem images with a clear explanation
	if (lower_name.endsWith(QStringLiteral(".pkg")) ||
	    lower_name.endsWith(QStringLiteral(".fpkg")) ||
	    lower_name.endsWith(QStringLiteral(".ffpkg")) ||
	    lower_name.endsWith(QStringLiteral(".ffpfs")) ||
	    lower_name.endsWith(QStringLiteral(".ffpfsc")) ||
	    lower_name.endsWith(QStringLiteral(".exfat"))) {
		result.error_message = QObject::tr(
		    "Console-only image format (%1). On PC, RazorEmulations runs unpacked PS5 game folders "
		    "(eboot.bin + sce_sys/) or .zar / .zip / .7z archives rather than ShadowMountPlus console images.")
		                           .arg(src_info.fileName());
		return result;
	}

	// Case 1: Native ZArchive (.zar) single-file PS5 package
	if (lower_name.endsWith(QStringLiteral(".zar"))) {
		const auto archive_reader = Common::OpenArchive(GameContent::ToPath(source_package_path));
		if (archive_reader == nullptr ||
		    !GameContent::FileExists(source_package_path, QStringLiteral("eboot.bin"))) {
			result.error_message = QObject::tr(
			    "Invalid .zar package: archive could not be opened or does not contain eboot.bin.");
			return result;
		}

		QString detected_tid = ReadTitleIdFromParamJson(GameContent::ReadFile(
		    source_package_path, QStringLiteral("sce_sys/param.json"), GameContent::MaxMetadataSize));
		if (detected_tid.isEmpty()) {
			detected_tid = preferred_title_id.trimmed().toUpper();
		}

		const QString base_stem = !detected_tid.isEmpty()
		                              ? detected_tid
		                              : SanitizeFolderName(src_info.completeBaseName());
		const QString dest_zar =
		    QDir::cleanPath(QDir(target_games_dir).filePath(base_stem + QStringLiteral(".zar")));
		const QString temp_zar = dest_zar + QStringLiteral(".installing");

		if (QFileInfo::exists(dest_zar) &&
		    QDir::cleanPath(src_info.absoluteFilePath())
		            .compare(dest_zar, Qt::CaseInsensitive) == 0) {
			result.ok             = true;
			result.installed_path = dest_zar;
			result.title_id       = detected_tid;
			return result;
		}

		QFile::remove(temp_zar);
		if (!QFile::copy(source_package_path, temp_zar)) {
			result.error_message =
			    QObject::tr("Failed to write package into Games folder: %1").arg(dest_zar);
			return result;
		}
		QFile::remove(dest_zar);
		if (!QFile::rename(temp_zar, dest_zar)) {
			QFile::remove(temp_zar);
			result.error_message =
			    QObject::tr("Failed to finalize installed package (is the game currently running?): %1")
			        .arg(dest_zar);
			return result;
		}

		result.ok             = true;
		result.installed_path = dest_zar;
		result.title_id       = detected_tid;
		return result;
	}

	// Case 2: Archive (.zip, .7z, .tar.gz, .tgz, .tar, .rar, multi-part .001)
	if (lower_name.endsWith(QStringLiteral(".zip")) ||
	    lower_name.endsWith(QStringLiteral(".7z")) ||
	    lower_name.contains(QStringLiteral(".7z.")) ||
	    lower_name.endsWith(QStringLiteral(".tar.gz")) ||
	    lower_name.endsWith(QStringLiteral(".tgz")) ||
	    lower_name.endsWith(QStringLiteral(".tar")) ||
	    lower_name.endsWith(QStringLiteral(".rar"))) {
		const QString staging_path = QDir(target_games_dir)
		                                 .filePath(QStringLiteral(".razor_stage_") +
		                                           QUuid::createUuid().toString(QUuid::Id128));
		if (!QDir().mkpath(staging_path)) {
			result.error_message = QObject::tr("Could not create temporary staging folder.");
			return result;
		}

		const auto cleanup_staging = [&staging_path]() { QDir(staging_path).removeRecursively(); };

		QString extract_err;
		if (!ExtractArchiveSafely(source_package_path, staging_path, &extract_err)) {
			cleanup_staging();
			result.error_message = extract_err;
			return result;
		}

		// Check if the archive contained a .zar package inside it:
		QDirIterator zar_it(staging_path, {QStringLiteral("*.zar")}, QDir::Files,
		                    QDirIterator::Subdirectories);
		if (zar_it.hasNext()) {
			const QString nested_zar = zar_it.next();
			auto          sub_res    = InstallPackageToDirectory(nested_zar, target_games_dir,
			                                                     preferred_title_id, preferred_name);
			cleanup_staging();
			return sub_res;
		}

		// Locate the game root folder containing eboot.bin:
		QString      found_game_root;
		QDirIterator eboot_it(staging_path, {QStringLiteral("eboot.bin")},
		                      QDir::Files | QDir::CaseSensitive, QDirIterator::Subdirectories);
		while (eboot_it.hasNext()) {
			const QFileInfo fi(eboot_it.next());
			found_game_root = fi.absolutePath();
			break;
		}
		if (found_game_root.isEmpty()) {
			// Case-insensitive fallback for Windows-packed archives:
			QDirIterator any_it(staging_path, QDir::Files, QDirIterator::Subdirectories);
			while (any_it.hasNext()) {
				const QFileInfo fi(any_it.next());
				if (fi.fileName().compare(QStringLiteral("eboot.bin"), Qt::CaseInsensitive) == 0) {
					found_game_root = fi.absolutePath();
					if (fi.fileName() != QStringLiteral("eboot.bin")) {
						QFile::rename(fi.absoluteFilePath(),
						              QDir(found_game_root).filePath(QStringLiteral("eboot.bin")));
					}
					break;
				}
			}
		}

		if (found_game_root.isEmpty()) {
			cleanup_staging();
			result.error_message = QObject::tr(
			    "Archive does not contain a PS5 Prospero title (missing eboot.bin or .zar package).");
			return result;
		}

		QString detected_tid = preferred_title_id.trimmed().toUpper();
		EnsureMinimalParamJson(found_game_root, detected_tid, preferred_name, &detected_tid);

		const QString folder_name = SanitizeFolderName(
		    !detected_tid.isEmpty() ? detected_tid
		                            : (!preferred_name.trimmed().isEmpty()
		                                   ? preferred_name
		                                   : src_info.completeBaseName()));
		const QString final_dir = QDir::cleanPath(QDir(target_games_dir).filePath(folder_name));

		if (QDir(final_dir).exists()) {
			QDir(final_dir).removeRecursively();
		}
		if (!QDir().rename(found_game_root, final_dir)) {
			cleanup_staging();
			result.error_message =
			    QObject::tr("Failed to move extracted game folder into %1").arg(final_dir);
			return result;
		}

		cleanup_staging();
		result.ok             = true;
		result.installed_path = final_dir;
		result.title_id       = detected_tid;
		return result;
	}

	// Case 3: Standalone PS5 homebrew ELF / eboot.bin
	if (lower_name.endsWith(QStringLiteral(".elf")) ||
	    lower_name.endsWith(QStringLiteral(".bin"))) {
		const QString folder_name = SanitizeFolderName(
		    !preferred_title_id.trimmed().isEmpty()
		        ? preferred_title_id
		        : (!preferred_name.trimmed().isEmpty() ? preferred_name
		                                               : src_info.completeBaseName()));
		const QString final_dir = QDir::cleanPath(QDir(target_games_dir).filePath(folder_name));
		QDir(final_dir).removeRecursively();
		if (!QDir().mkpath(final_dir)) {
			result.error_message = QObject::tr("Failed to create folder: %1").arg(final_dir);
			return result;
		}
		const QString dest_eboot = QDir(final_dir).filePath(QStringLiteral("eboot.bin"));
		if (!QFile::copy(source_package_path, dest_eboot)) {
			result.error_message = QObject::tr("Failed to copy homebrew binary to eboot.bin.");
			return result;
		}
		QString detected_tid = preferred_title_id.trimmed().toUpper();
		EnsureMinimalParamJson(final_dir, detected_tid, preferred_name, &detected_tid);

		result.ok             = true;
		result.installed_path = final_dir;
		result.title_id       = detected_tid;
		return result;
	}

	result.error_message = QObject::tr(
	    "Unsupported file type (%1). Supported formats: .zar, .zip, .7z, .tar.gz, .elf, .bin.")
	                           .arg(src_info.fileName());
	return result;
}

} // namespace RazorStore

class RazorStoreDialog::Private: public QObject {
	Q_OBJECT

public:
	RazorStoreDialog*     dialog      = nullptr;
	IRazorStoreLibrary*   list_widget = nullptr;
	QString               settings_file;
	QNetworkAccessManager nam;

	QLabel*      install_dir_label = nullptr;
	QLineEdit*   search_edit       = nullptr;
	QComboBox*   source_combo      = nullptr;
	QComboBox*   category_combo    = nullptr;
	QCheckBox*   prospero_toggle   = nullptr;
	QListWidget* catalog_list      = nullptr;

	QLabel*       cover_label       = nullptr;
	QLabel*       title_label       = nullptr;
	QLabel*       subtitle_label    = nullptr;
	QLabel*       badges_label      = nullptr;
	QLabel*       meta_label        = nullptr;
	QLabel*       controls_label    = nullptr;
	QLabel*       description_label = nullptr;
	QProgressBar* progress_bar      = nullptr;
	QLabel*       status_label      = nullptr;
	QPushButton*  install_button    = nullptr;
	QPushButton*  launch_button     = nullptr;

	QLineEdit*   direct_url_edit     = nullptr;
	QPushButton* direct_url_button   = nullptr;
	QPushButton* local_import_button = nullptr;

	QVector<RazorStoreItem> all_items;
	QVector<int>            filtered_indices;
	QMap<QString, QPixmap>  icon_cache;
	QSet<QString>           pending_icon_keys;
	QStringList             custom_catalog_urls;
	QStringList             local_catalog_files;
	bool                    enable_prospero_feed = true;
	QPointer<QNetworkReply> active_download;
	QTimer*                 icon_fetch_timer = nullptr;
	bool                    busy             = false;

	explicit Private(RazorStoreDialog* owner): QObject(owner), dialog(owner) {}

	void SetupUi() {
		dialog->setWindowTitle(
		    tr("Razor Store — PS5 Homebrew, ps5-library Catalogs & Direct Auto-Installer"));
		dialog->resize(1080, 680);
		dialog->setMinimumSize(900, 580);

		icon_fetch_timer = new QTimer(this);
		icon_fetch_timer->setSingleShot(true);
		icon_fetch_timer->setInterval(90);
		connect(icon_fetch_timer, &QTimer::timeout, this, &Private::FetchVisibleIcons);

		auto* root = new QVBoxLayout(dialog);
		root->setContentsMargins(14, 14, 14, 14);
		root->setSpacing(10);

		// 1. Luxury Gunmetal Command Banner
		auto* header_frame = new QFrame(dialog);
		header_frame->setObjectName(QStringLiteral("razor_header_frame"));
		auto* header_layout = new QHBoxLayout(header_frame);
		header_layout->setContentsMargins(14, 10, 14, 10);
		header_layout->setSpacing(10);

		auto* store_logo_label = new QLabel(header_frame);
		store_logo_label->setFixedSize(44, 44);
		const QIcon store_brand_icon(QStringLiteral(":/icons/razor-logo.png"));
		store_logo_label->setPixmap(
		    store_brand_icon.pixmap(QSize(44, 44), dialog->devicePixelRatioF()));
		header_layout->addWidget(store_logo_label);

		auto* title_col = new QVBoxLayout();
		title_col->setSpacing(2);
		auto* store_title =
		    new QLabel(tr("✦  RAZOR STORE  ·  PS5-LIBRARY && PROSPERO HUB"), header_frame);
		store_title->setObjectName(QStringLiteral("brand_title_label"));
		install_dir_label = new QLabel(header_frame);
		install_dir_label->setObjectName(QStringLiteral("resolution_hint_label"));
		title_col->addWidget(store_title);
		title_col->addWidget(install_dir_label);
		header_layout->addLayout(title_col, 1);

		auto* open_dir_btn = new QPushButton(tr("Open Games Folder"), header_frame);
		open_dir_btn->setCursor(Qt::PointingHandCursor);
		auto* load_json_btn = new QPushButton(tr("+ Load Catalog .json"), header_frame);
		load_json_btn->setCursor(Qt::PointingHandCursor);
		load_json_btn->setToolTip(tr(
		    "Import a local catalog .json file (supports deox1111/ps5-library, Pegasus DL, ProsperoStore, and Razor Store schemas)"));
		auto* add_source_btn = new QPushButton(tr("+ Catalog URL / Key"), header_frame);
		add_source_btn->setCursor(Qt::PointingHandCursor);
		add_source_btn->setToolTip(tr(
		    "Add remote catalog.json URLs (supports ps5-library, Pegasus DL, ProsperoStore, or LAN Vaults; append |BEARER_TOKEN for private feeds)"));
		auto* refresh_btn = new QPushButton(tr("↻ Refresh"), header_frame);
		refresh_btn->setCursor(Qt::PointingHandCursor);

		header_layout->addWidget(open_dir_btn);
		header_layout->addWidget(load_json_btn);
		header_layout->addWidget(add_source_btn);
		header_layout->addWidget(refresh_btn);
		root->addWidget(header_frame);

		// 2. Search, Source & Category Filter Row
		auto* filter_row = new QHBoxLayout();
		filter_row->setSpacing(10);
		search_edit = new QLineEdit(dialog);
		search_edit->setPlaceholderText(
		    tr("Search 200+ PS5 homebrew games, ports, PPSA title IDs, or custom ps5-library entries..."));
		search_edit->setClearButtonEnabled(true);

		source_combo = new QComboBox(dialog);
		source_combo->setMinimumWidth(195);
		source_combo->addItem(tr("All Store Sources"));
		source_combo->addItem(tr("Razor Official"));
		source_combo->addItem(tr("PS5 Homebrew (ps5-library)"));
		source_combo->addItem(tr("Custom Catalogs"));

		category_combo = new QComboBox(dialog);
		category_combo->setMinimumWidth(185);
		category_combo->addItem(tr("All Categories"));
		category_combo->addItem(tr("Games && Ports"));
		category_combo->addItem(tr("Apps && Emulators"));
		category_combo->addItem(tr("Demos && Diagnostics"));
		category_combo->addItem(tr("Installed in Library"));

		prospero_toggle = new QCheckBox(tr("Live ps5-library Feed (200+)"), dialog);
		prospero_toggle->setChecked(true);
		prospero_toggle->setToolTip(
		    tr("Include the live public PS5 Homebrew Catalog (prosperostore.net) used by deox1111/ps5-library"));

		filter_row->addWidget(search_edit, 1);
		filter_row->addWidget(source_combo);
		filter_row->addWidget(category_combo);
		filter_row->addWidget(prospero_toggle);
		root->addLayout(filter_row);

		// 3. Master-Detail Splitter (Catalog List on Left, Rich Game Inspector on Right)
		auto* splitter = new QSplitter(Qt::Horizontal, dialog);
		splitter->setChildrenCollapsible(false);

		catalog_list = new QListWidget(splitter);
		catalog_list->setIconSize(QSize(56, 56));
		catalog_list->setSpacing(4);
		catalog_list->setUniformItemSizes(false);
		catalog_list->setSelectionMode(QAbstractItemView::SingleSelection);
		catalog_list->setMinimumWidth(380);
		if (catalog_list->verticalScrollBar() != nullptr) {
			connect(catalog_list->verticalScrollBar(), &QScrollBar::valueChanged, this,
			        [this]() { icon_fetch_timer->start(); });
		}

		auto* detail_card = new QFrame(splitter);
		detail_card->setObjectName(QStringLiteral("razor_header_frame"));
		auto* detail_layout = new QVBoxLayout(detail_card);
		detail_layout->setContentsMargins(18, 16, 18, 16);
		detail_layout->setSpacing(10);

		auto* hero_row = new QHBoxLayout();
		hero_row->setSpacing(16);
		cover_label = new QLabel(detail_card);
		cover_label->setFixedSize(136, 136);
		cover_label->setScaledContents(false);
		cover_label->setAlignment(Qt::AlignCenter);
		hero_row->addWidget(cover_label);

		auto* hero_meta = new QVBoxLayout();
		hero_meta->setSpacing(4);
		title_label = new QLabel(detail_card);
		title_label->setObjectName(QStringLiteral("brand_title_label"));
		title_label->setWordWrap(true);
		subtitle_label = new QLabel(detail_card);
		subtitle_label->setObjectName(QStringLiteral("brand_subtitle_label"));
		subtitle_label->setWordWrap(true);
		badges_label = new QLabel(detail_card);
		badges_label->setObjectName(QStringLiteral("resolution_hint_label"));
		badges_label->setWordWrap(true);
		meta_label = new QLabel(detail_card);
		meta_label->setWordWrap(true);
		meta_label->setStyleSheet(QStringLiteral("color: #A8B1BE; font-size: 12px;"));

		hero_meta->addWidget(title_label);
		hero_meta->addWidget(subtitle_label);
		hero_meta->addWidget(badges_label);
		hero_meta->addWidget(meta_label);
		hero_meta->addStretch(1);
		hero_row->addLayout(hero_meta, 1);
		detail_layout->addLayout(hero_row);

		auto* sep = new QFrame(detail_card);
		sep->setFrameShape(QFrame::HLine);
		sep->setStyleSheet(QStringLiteral("background-color: #2E3542; max-height: 1px;"));
		detail_layout->addWidget(sep);

		controls_label = new QLabel(detail_card);
		controls_label->setWordWrap(true);
		controls_label->setStyleSheet(
		    QStringLiteral("color: #D6B26C; font-weight: 600; font-size: 12px;"));
		detail_layout->addWidget(controls_label);

		description_label = new QLabel(detail_card);
		description_label->setWordWrap(true);
		description_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		description_label->setStyleSheet(
		    QStringLiteral("color: #E3E8F0; font-size: 13px; line-height: 1.4;"));
		detail_layout->addWidget(description_label, 1);

		progress_bar = new QProgressBar(detail_card);
		progress_bar->setRange(0, 100);
		progress_bar->setValue(0);
		progress_bar->setVisible(false);
		detail_layout->addWidget(progress_bar);

		status_label = new QLabel(
		    tr("Select any PS5 title to download, verify SHA-256, and auto-install directly into your Games library."),
		    detail_card);
		status_label->setWordWrap(true);
		status_label->setStyleSheet(QStringLiteral("color: #9AA3B2; font-size: 12px;"));
		detail_layout->addWidget(status_label);

		auto* action_row = new QHBoxLayout();
		action_row->setSpacing(10);
		install_button = new QPushButton(tr("⬇  DOWNLOAD && AUTO-INSTALL"), detail_card);
		install_button->setObjectName(QStringLiteral("launch_game_button"));
		install_button->setMinimumHeight(40);
		install_button->setCursor(Qt::PointingHandCursor);

		launch_button = new QPushButton(tr("▶  PLAY NOW"), detail_card);
		launch_button->setObjectName(QStringLiteral("razor_store_button"));
		launch_button->setMinimumHeight(40);
		launch_button->setCursor(Qt::PointingHandCursor);
		launch_button->setEnabled(false);

		action_row->addWidget(install_button, 2);
		action_row->addWidget(launch_button, 1);
		detail_layout->addLayout(action_row);

		splitter->addWidget(catalog_list);
		splitter->addWidget(detail_card);
		splitter->setStretchFactor(0, 4);
		splitter->setStretchFactor(1, 6);
		root->addWidget(splitter, 1);

		// 4. Direct Package URL & Local Archive Auto-Installer Footer
		auto* direct_frame = new QFrame(dialog);
		direct_frame->setObjectName(QStringLiteral("razor_footer_frame"));
		auto* direct_layout = new QHBoxLayout(direct_frame);
		direct_layout->setContentsMargins(12, 8, 12, 8);
		direct_layout->setSpacing(10);

		auto* direct_lbl = new QLabel(tr("Direct URL / Local Package:"), direct_frame);
		direct_lbl->setObjectName(QStringLiteral("control_caption_label"));
		direct_url_edit = new QLineEdit(direct_frame);
		direct_url_edit->setPlaceholderText(
		    tr("Paste any direct https://... (.zar, .zip, .7z, .tar.gz) package link to download && auto-install..."));
		direct_url_button = new QPushButton(tr("Download URL"), direct_frame);
		direct_url_button->setCursor(Qt::PointingHandCursor);
		local_import_button = new QPushButton(tr("Import Local Package..."), direct_frame);
		local_import_button->setCursor(Qt::PointingHandCursor);

		direct_layout->addWidget(direct_lbl);
		direct_layout->addWidget(direct_url_edit, 1);
		direct_layout->addWidget(direct_url_button);
		direct_layout->addWidget(local_import_button);
		root->addWidget(direct_frame);

		// Connections
		connect(open_dir_btn, &QPushButton::clicked, this, [this]() {
			const QString dir = TargetGamesDir();
			QDir().mkpath(dir);
			QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
		});
		connect(load_json_btn, &QPushButton::clicked, this, &Private::ImportLocalCatalogJson);
		connect(add_source_btn, &QPushButton::clicked, this, &Private::PromptCustomCatalogUrl);
		connect(refresh_btn, &QPushButton::clicked, this, &Private::ReloadCatalogs);
		connect(search_edit, &QLineEdit::textChanged, this, &Private::RebuildList);
		connect(source_combo, &QComboBox::currentIndexChanged, this, &Private::RebuildList);
		connect(category_combo, &QComboBox::currentIndexChanged, this, &Private::RebuildList);
		connect(prospero_toggle, &QCheckBox::toggled, this, [this](bool checked) {
			enable_prospero_feed = checked;
			SaveCustomCatalogSettings();
			ReloadCatalogs();
		});
		connect(catalog_list, &QListWidget::currentRowChanged, this, [this]() {
			UpdateInspector();
			icon_fetch_timer->start();
		});
		connect(install_button, &QPushButton::clicked, this, &Private::InstallSelectedItem);
		connect(launch_button, &QPushButton::clicked, this, &Private::LaunchSelectedItem);
		connect(direct_url_button, &QPushButton::clicked, this, &Private::InstallFromDirectUrl);
		connect(local_import_button, &QPushButton::clicked, this, &Private::ImportLocalPackage);

		LoadCustomCatalogSettings();
		prospero_toggle->blockSignals(true);
		prospero_toggle->setChecked(enable_prospero_feed);
		prospero_toggle->blockSignals(false);
		UpdateInstallDirBanner();
		ReloadCatalogs();
	}

	QString TargetGamesDir() const {
		if (list_widget != nullptr) {
			return list_widget->GetPrimaryInstallDirectory();
		}
		const QString fallback = QDir::cleanPath(
		    QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("Games")));
		QDir().mkpath(fallback);
		return fallback;
	}

	void UpdateInstallDirBanner() {
		const int custom_count = custom_catalog_urls.size() + local_catalog_files.size();
		const QString feed_summary =
		    custom_count > 0
		        ? tr("  ·  +%1 Custom Catalog(s) Active").arg(custom_count)
		        : tr("  ·  Supports ps5-library, ProsperoStore && Pegasus DL JSON");
		install_dir_label->setText(
		    tr("Auto-Install Target: %1%2")
		        .arg(QDir::toNativeSeparators(TargetGamesDir()), feed_summary));
	}

	void LoadCustomCatalogSettings() {
		if (settings_file.isEmpty()) {
			return;
		}
		QSettings s(settings_file, QSettings::IniFormat);
		s.beginGroup(SETTINGS_STORE_GROUP);
		custom_catalog_urls  = s.value(SETTINGS_CUSTOM_CATALOGS).toStringList();
		local_catalog_files  = s.value(SETTINGS_LOCAL_CATALOG_FILES).toStringList();
		enable_prospero_feed = s.value(SETTINGS_ENABLE_PROSPERO_FEED, true).toBool();
		custom_catalog_urls.removeAll(QString());
		local_catalog_files.removeAll(QString());
		s.endGroup();
	}

	void SaveCustomCatalogSettings() const {
		if (settings_file.isEmpty()) {
			return;
		}
		QSettings s(settings_file, QSettings::IniFormat);
		s.beginGroup(SETTINGS_STORE_GROUP);
		s.setValue(SETTINGS_CUSTOM_CATALOGS, custom_catalog_urls);
		s.setValue(SETTINGS_LOCAL_CATALOG_FILES, local_catalog_files);
		s.setValue(SETTINGS_ENABLE_PROSPERO_FEED, enable_prospero_feed);
		s.endGroup();
	}

	void ImportLocalCatalogJson() {
		const QString path = QFileDialog::getOpenFileName(
		    dialog,
		    tr("Load Catalog JSON (ps5-library / Pegasus DL / ProsperoStore / Razor Store)"), {},
		    tr("Catalog JSON (*.json);;All Files (*)"));
		if (path.isEmpty()) {
			return;
		}
		QFile f(path);
		if (!f.open(QIODevice::ReadOnly)) {
			QMessageBox::warning(dialog, tr("Catalog Load Failed"),
			                     tr("Could not read file: %1").arg(path));
			return;
		}
		QString                 store_name;
		QVector<RazorStoreItem> parsed;
		QString                 err;
		QStringList             nested_urls;
		if (!RazorStore::ParseCatalogJson(f.read(MAX_CATALOG_BYTES), QFileInfo(path).absolutePath(),
		                                  &store_name, &parsed, &err, &nested_urls)) {
			QMessageBox::warning(dialog, tr("Invalid Catalog JSON"), err);
			return;
		}

		if (!nested_urls.isEmpty()) {
			for (const QString& nu: nested_urls) {
				if (!custom_catalog_urls.contains(nu)) {
					custom_catalog_urls.push_back(nu);
				}
			}
			SaveCustomCatalogSettings();
			UpdateInstallDirBanner();
			ReloadCatalogs();
			status_label->setText(
			    tr("✓ Added %1 catalog URL(s) from source list \"%2\".")
			        .arg(nested_urls.size())
			        .arg(store_name));
			return;
		}

		const QString clean_path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
		if (!local_catalog_files.contains(clean_path)) {
			local_catalog_files.push_back(clean_path);
			SaveCustomCatalogSettings();
		}
		UpdateInstallDirBanner();
		ReloadCatalogs();
		source_combo->setCurrentIndex(3); // Switch to Custom Catalogs view so user sees imported games immediately
		status_label->setText(
		    tr("✓ Loaded %1 PS5 title(s) from \"%2\".").arg(parsed.size()).arg(store_name));
	}

	void PromptCustomCatalogUrl() {
		bool          ok      = false;
		const QString current = custom_catalog_urls.join(QStringLiteral("; "));
		const QString input   = QInputDialog::getText(
		    dialog, tr("Manage Catalog URLs (ps5-library / Pegasus DL / Razor Vault)"),
		    tr("Enter one or more catalog.json URLs separated by ';'.\n"
		         "• Supports deox1111/ps5-library, Pegasus DL, ProsperoStore, and Razor Store JSON.\n"
		         "• Optional Bearer key: https://host/catalog.json|YOUR_ACCESS_KEY\n"
		         "• Clear field to remove custom URLs and local catalog files:"),
		    QLineEdit::Normal, current, &ok);
		if (!ok) {
			return;
		}

		if (input.trimmed().isEmpty()) {
			custom_catalog_urls.clear();
			local_catalog_files.clear();
			SaveCustomCatalogSettings();
			UpdateInstallDirBanner();
			ReloadCatalogs();
			return;
		}

		QStringList updated;
		for (const QString& part: input.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
			QString url_part;
			QString bearer_part;
			SplitCatalogSpecAndBearer(part, &url_part, &bearer_part);
			const QUrl url(url_part);
			if (url.isValid() &&
			    (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http") ||
			     url.isLocalFile())) {
				updated.push_back(bearer_part.isEmpty()
				                      ? url_part
				                      : QStringLiteral("%1|%2").arg(url_part, bearer_part));
			}
		}
		updated.removeDuplicates();
		custom_catalog_urls = updated;
		SaveCustomCatalogSettings();
		UpdateInstallDirBanner();
		ReloadCatalogs();
	}

	void MergeCatalogItems(const QVector<RazorStoreItem>& incoming) {
		for (const RazorStoreItem& item: incoming) {
			int existing_idx = -1;
			for (int i = 0; i < all_items.size(); ++i) {
				// Match same Razor official title_id or identical entry_id
				if (!item.entry_id.isEmpty() && all_items[i].entry_id == item.entry_id) {
					existing_idx = i;
					break;
				}
				if (item.source_kind == QStringLiteral("razor") &&
				    all_items[i].source_kind == QStringLiteral("razor") &&
				    !item.title_id.isEmpty() && all_items[i].title_id == item.title_id) {
					existing_idx = i;
					break;
				}
			}
			if (existing_idx >= 0) {
				RazorStoreItem merged = item;
				if (merged.local_fallback_path.isEmpty()) {
					merged.local_fallback_path = all_items[existing_idx].local_fallback_path;
				}
				if (merged.local_icon_path.isEmpty()) {
					merged.local_icon_path = all_items[existing_idx].local_icon_path;
				}
				all_items[existing_idx] = std::move(merged);
			} else {
				all_items.push_back(item);
			}
		}
	}

	void FetchRemoteCatalogSpec(const QString& spec, const QString& bundled_dir,
	                            const QString& override_source_kind = {}) {
		QString url_str;
		QString bearer;
		SplitCatalogSpecAndBearer(spec, &url_str, &bearer);

		const QUrl url(url_str);
		if (!url.isValid()) {
			return;
		}
		if (url.isLocalFile()) {
			const QFileInfo fi(url.toLocalFile());
			QFile           f(fi.absoluteFilePath());
			if (f.open(QIODevice::ReadOnly)) {
				QString                 name;
				QVector<RazorStoreItem> items;
				if (RazorStore::ParseCatalogJson(f.read(MAX_CATALOG_BYTES), fi.absolutePath(),
				                                 &name, &items)) {
					if (!override_source_kind.isEmpty()) {
						for (auto& it: items) {
							it.source_kind = override_source_kind;
						}
					}
					MergeCatalogItems(items);
					RebuildList();
				}
			}
			return;
		}

		QNetworkRequest req(url);
		req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("RazorEmulations/1.0"));
		if (!bearer.isEmpty()) {
			req.setRawHeader("Authorization", QByteArray("Bearer ") + bearer.toUtf8());
		}
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		                 QNetworkRequest::NoLessSafeRedirectPolicy);
		req.setTransferTimeout(15000);
		QNetworkReply* reply = nam.get(req);
		connect(reply, &QNetworkReply::finished, this,
		        [this, reply, bundled_dir, override_source_kind]() {
			        reply->deleteLater();
			        if (reply->error() != QNetworkReply::NoError) {
				        return;
			        }
			        const QByteArray        payload = reply->read(MAX_CATALOG_BYTES);
			        QString                 name;
			        QVector<RazorStoreItem> remote_items;
			        QStringList             nested_sources;
			        if (RazorStore::ParseCatalogJson(payload, bundled_dir, &name, &remote_items,
			                                         nullptr, &nested_sources)) {
				        if (!nested_sources.isEmpty()) {
					        for (const QString& ns: nested_sources) {
						        FetchRemoteCatalogSpec(ns, {}, QStringLiteral("custom"));
					        }
					        return;
				        }
				        if (!override_source_kind.isEmpty()) {
					        for (auto& it: remote_items) {
						        it.source_kind = override_source_kind;
					        }
				        }
				        MergeCatalogItems(remote_items);
				        RebuildList();
			        }
		        });
	}

	void ReloadCatalogs() {
		all_items.clear();

		// 1. Load bundled local store/catalog.json immediately for instant 0-latency & offline support
		const QString bundled_dir = RazorStore::LocateBundledStoreDir();
		if (!bundled_dir.isEmpty()) {
			QFile local_cat(QDir(bundled_dir).filePath(QStringLiteral("catalog.json")));
			if (local_cat.open(QIODevice::ReadOnly)) {
				QString                 name;
				QVector<RazorStoreItem> local_items;
				if (RazorStore::ParseCatalogJson(local_cat.readAll(), bundled_dir, &name,
				                                 &local_items)) {
					MergeCatalogItems(local_items);
				}
			}
		}

		// 2. Load any imported local .json catalog files (ps5-library / Pegasus DL / custom)
		for (const QString& local_json: std::as_const(local_catalog_files)) {
			const QFileInfo fi(local_json);
			QFile           f(fi.absoluteFilePath());
			if (f.open(QIODevice::ReadOnly)) {
				QString                 name;
				QVector<RazorStoreItem> items;
				if (RazorStore::ParseCatalogJson(f.read(MAX_CATALOG_BYTES), fi.absolutePath(),
				                                 &name, &items)) {
					for (auto& it: items) {
						if (it.source_kind == QStringLiteral("razor")) {
							it.source_kind = QStringLiteral("custom");
						}
					}
					MergeCatalogItems(items);
				}
			}
		}

		RebuildList();

		// 3. Fetch live Official Razor catalog
		FetchRemoteCatalogSpec(QString::fromLatin1(OFFICIAL_CATALOG_URL), bundled_dir,
		                       QStringLiteral("razor"));

		// 4. Fetch live PS5 Homebrew Catalog (prosperostore.net — default catalog in deox1111/ps5-library)
		if (enable_prospero_feed) {
			FetchRemoteCatalogSpec(QString::fromLatin1(PROSPERO_HOMEBREW_CATALOG_URL), {},
			                       QStringLiteral("prospero"));
		}

		// 5. Fetch any user-configured custom catalog URLs (with optional Bearer keys)
		for (const QString& custom_spec: std::as_const(custom_catalog_urls)) {
			FetchRemoteCatalogSpec(custom_spec, {}, QStringLiteral("custom"));
		}
	}

	QString IconKeyBase(const RazorStoreItem& item) const {
		return !item.entry_id.isEmpty() ? item.entry_id : item.title_id;
	}

	QPixmap CachedOrFallbackIcon(const RazorStoreItem& item, int px) {
		const QString base_key = IconKeyBase(item);
		const QString sized    = QStringLiteral("%1_%2").arg(base_key, QString::number(px));
		if (icon_cache.contains(sized)) {
			return icon_cache.value(sized);
		}
		if (!item.local_icon_path.isEmpty() && QFileInfo::exists(item.local_icon_path)) {
			QPixmap loaded(item.local_icon_path);
			if (!loaded.isNull()) {
				const QPixmap scaled =
				    loaded.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				icon_cache.insert(sized, scaled);
				return scaled;
			}
		}
		return MakeFallbackCoverPixmap(item.name, item.title_id, px);
	}

	void RequestIconDownloadForItem(const RazorStoreItem& item) {
		if (item.icon_url.isEmpty() || pending_icon_keys.size() >= MAX_CONCURRENT_ICON_DOWNLOADS) {
			return;
		}
		const QString base_key = IconKeyBase(item);
		if (icon_cache.contains(base_key + QStringLiteral("_56")) ||
		    pending_icon_keys.contains(base_key)) {
			return;
		}
		const QUrl url(item.icon_url);
		if (!url.isValid() ||
		    (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http"))) {
			return;
		}

		pending_icon_keys.insert(base_key);
		QNetworkRequest req(url);
		req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("RazorEmulations/1.0"));
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		                 QNetworkRequest::NoLessSafeRedirectPolicy);
		req.setTransferTimeout(10000);
		QNetworkReply* reply = nam.get(req);
		connect(reply, &QNetworkReply::finished, this, [this, reply, base_key]() {
			reply->deleteLater();
			pending_icon_keys.remove(base_key);
			if (reply->error() == QNetworkReply::NoError) {
				QPixmap net_pix;
				if (net_pix.loadFromData(reply->read(3 * 1024 * 1024))) {
					const QPixmap p56 =
					    net_pix.scaled(56, 56, Qt::KeepAspectRatio, Qt::SmoothTransformation);
					const QPixmap p136 =
					    net_pix.scaled(136, 136, Qt::KeepAspectRatio, Qt::SmoothTransformation);
					icon_cache.insert(base_key + QStringLiteral("_56"), p56);
					icon_cache.insert(base_key + QStringLiteral("_136"), p136);

					// Update matching list item & inspector in-place without resetting scroll!
					for (int row = 0; row < filtered_indices.size(); ++row) {
						const int idx = filtered_indices[row];
						if (idx >= 0 && idx < all_items.size() &&
						    IconKeyBase(all_items[idx]) == base_key) {
							if (auto* li = catalog_list->item(row); li != nullptr) {
								li->setIcon(QIcon(p56));
							}
						}
					}
					if (const auto* sel = SelectedItem();
					    sel != nullptr && IconKeyBase(*sel) == base_key) {
						cover_label->setPixmap(p136);
					}
				}
			}
			// Continue draining visible viewport icons if any remain uncached
			if (!pending_icon_keys.isEmpty() || catalog_list->count() > 0) {
				icon_fetch_timer->start();
			}
		});
	}

	void FetchVisibleIcons() {
		if (const auto* sel = SelectedItem(); sel != nullptr) {
			RequestIconDownloadForItem(*sel);
		}
		if (catalog_list->viewport() == nullptr || catalog_list->count() == 0) {
			return;
		}
		const QRect vp = catalog_list->viewport()->rect();
		for (int row = 0; row < catalog_list->count(); ++row) {
			if (pending_icon_keys.size() >= MAX_CONCURRENT_ICON_DOWNLOADS) {
				break;
			}
			auto* li = catalog_list->item(row);
			if (li == nullptr) {
				continue;
			}
			if (catalog_list->visualItemRect(li).intersects(vp)) {
				const int idx = filtered_indices.value(row, -1);
				if (idx >= 0 && idx < all_items.size()) {
					RequestIconDownloadForItem(all_items[idx]);
				}
			}
		}
	}

	void RebuildList() {
		const QString selected_entry =
		    SelectedItem() != nullptr ? SelectedItem()->entry_id : QString();
		const QMap<QString, QString> installed_map =
		    list_widget != nullptr ? list_widget->GetInstalledTitleVersions()
		                           : QMap<QString, QString> {};
		const QString query      = search_edit->text().trimmed();
		const int     src_filter = source_combo->currentIndex();
		const int     cat_filter = category_combo->currentIndex();

		catalog_list->blockSignals(true);
		catalog_list->clear();
		filtered_indices.clear();

		int row_to_select = 0;
		for (int i = 0; i < all_items.size(); ++i) {
			const RazorStoreItem& item = all_items[i];
			const bool is_installed    = !item.title_id.isEmpty() && installed_map.contains(item.title_id);

			if (src_filter == 1 && item.source_kind != QStringLiteral("razor")) {
				continue;
			}
			if (src_filter == 2 && item.source_kind != QStringLiteral("prospero")) {
				continue;
			}
			if (src_filter == 3 && item.source_kind != QStringLiteral("custom")) {
				continue;
			}

			const bool is_diag = item.category.contains(QStringLiteral("Diagnostic"), Qt::CaseInsensitive) ||
			                     item.category.contains(QStringLiteral("Demo"), Qt::CaseInsensitive);
			const bool is_app  = item.category.startsWith(QStringLiteral("App"), Qt::CaseInsensitive) ||
			                     item.category.contains(QStringLiteral("Homebrew App"), Qt::CaseInsensitive);

			if (cat_filter == 1 && (is_diag || is_app)) {
				continue; // Games & Ports only
			}
			if (cat_filter == 2 && !is_app) {
				continue; // Apps & Emulators only
			}
			if (cat_filter == 3 && !is_diag) {
				continue; // Demos & Diagnostics only
			}
			if (cat_filter == 4 && !is_installed) {
				continue; // Installed only
			}

			if (!query.isEmpty()) {
				const bool match = item.name.contains(query, Qt::CaseInsensitive) ||
				                   item.title_id.contains(query, Qt::CaseInsensitive) ||
				                   item.category.contains(query, Qt::CaseInsensitive) ||
				                   item.author.contains(query, Qt::CaseInsensitive) ||
				                   item.description.contains(query, Qt::CaseInsensitive) ||
				                   item.source_name.contains(query, Qt::CaseInsensitive);
				if (!match) {
					continue;
				}
			}

			const QString status_tag =
			    is_installed ? tr("  [INSTALLED ✓]")
			                 : (item.size_bytes > 0
			                        ? QStringLiteral("  [%1]").arg(FormatByteSize(item.size_bytes))
			                        : QString());
			const QString tid_label =
			    item.title_id.isEmpty() ? QStringLiteral("PS5") : item.title_id;
			const QString label = QStringLiteral("%1%2\n%3  ·  %4  ·  v%5")
			                          .arg(item.name, status_tag, tid_label, item.category,
			                               item.version);

			auto* list_item = new QListWidgetItem(QIcon(CachedOrFallbackIcon(item, 56)), label);
			list_item->setSizeHint(QSize(340, 68));
			if (is_installed) {
				list_item->setForeground(QColor(0xD6, 0xB2, 0x6C));
			}
			catalog_list->addItem(list_item);
			filtered_indices.push_back(i);

			if (!selected_entry.isEmpty() && item.entry_id == selected_entry) {
				row_to_select = filtered_indices.size() - 1;
			}
		}

		if (catalog_list->count() > 0) {
			catalog_list->setCurrentRow(row_to_select);
		}
		catalog_list->blockSignals(false);
		UpdateInspector();
		icon_fetch_timer->start();
	}

	const RazorStoreItem* SelectedItem() const {
		const int row = catalog_list->currentRow();
		if (row < 0 || row >= filtered_indices.size()) {
			return nullptr;
		}
		const int idx = filtered_indices[row];
		return (idx >= 0 && idx < all_items.size()) ? &all_items[idx] : nullptr;
	}

	void UpdateInspector() {
		const RazorStoreItem* item = SelectedItem();
		if (item == nullptr) {
			cover_label->setPixmap(MakeFallbackCoverPixmap(QStringLiteral("RZ"), {}, 136));
			title_label->setText(tr("No Store Title Selected (%1 Total Loaded)")
			                         .arg(all_items.size()));
			subtitle_label->setText(tr(
			    "Adjust your search/filter above, load a ps5-library catalog.json, or paste a direct .zar / .zip / .7z URL below."));
			badges_label->clear();
			meta_label->clear();
			controls_label->setText(tr("◆  Controls: DualSense Controller or Mapped PC Keyboard"));
			description_label->clear();
			install_button->setEnabled(false);
			launch_button->setEnabled(false);
			return;
		}

		const QMap<QString, QString> installed_map =
		    list_widget != nullptr ? list_widget->GetInstalledTitleVersions()
		                           : QMap<QString, QString> {};
		const bool is_installed = !item->title_id.isEmpty() && installed_map.contains(item->title_id);

		cover_label->setPixmap(CachedOrFallbackIcon(*item, 136));
		title_label->setText(item->name);
		subtitle_label->setText(item->subtitle);
		badges_label->setText(
		    QStringLiteral("<b>%1</b> &nbsp;·&nbsp; %2 &nbsp;·&nbsp; %3")
		        .arg((item->title_id.isEmpty() ? QStringLiteral("PS5") : item->title_id)
		                 .toHtmlEscaped(),
		             item->source_name.toHtmlEscaped(),
		             is_installed
		                 ? tr("<span style='color:#63D297;'>INSTALLED IN LIBRARY ✓</span>")
		                 : (item->console_only_format
		                        ? tr("<span style='color:#E6A15C;'>CONSOLE IMAGE FORMAT</span>")
		                        : tr("Ready to Install"))));
		meta_label->setText(
		    tr("Version %1  ·  Package: .%2 (%3)  ·  Author: %4  ·  License: %5")
		        .arg(item->version, item->format.toUpper(), FormatByteSize(item->size_bytes),
		             item->author, item->license));
		controls_label->setText(tr("◆  Controls: %1").arg(item->controls));
		description_label->setText(item->description);

		install_button->setEnabled(!busy && !item->console_only_format);
		if (item->console_only_format) {
			install_button->setText(tr("⚠  CONSOLE IMAGE (.%1 — UNPACK FOR PC)").arg(item->format.toUpper()));
		} else {
			install_button->setText(is_installed ? tr("↻  REINSTALL / VERIFY PACKAGE")
			                                     : tr("⬇  DOWNLOAD && AUTO-INSTALL"));
		}
		launch_button->setEnabled(!busy && is_installed);
	}

	void SetBusyState(bool is_busy, const QString& message) {
		busy = is_busy;
		progress_bar->setVisible(is_busy);
		status_label->setText(message);
		direct_url_button->setEnabled(!is_busy);
		local_import_button->setEnabled(!is_busy);
		UpdateInspector();
	}

	void FinishInstallAndRefresh(const RazorStoreInstallResult& res, const QString& display_name) {
		if (!res.ok) {
			SetBusyState(false, tr("Install failed: %1").arg(res.error_message));
			QMessageBox::warning(dialog, tr("Razor Store Install Failed"), res.error_message);
			return;
		}

		if (list_widget != nullptr) {
			list_widget->ScanGameDirectory();
			list_widget->SelectGameByTitleIdOrPath(res.title_id, res.installed_path);
		}
		UpdateInstallDirBanner();
		RebuildList();
		SetBusyState(false,
		             tr("✓ Installed \"%1\" into %2 — ready to play!")
		                 .arg(display_name, QDir::toNativeSeparators(res.installed_path)));
	}

	void InstallSelectedItem() {
		const RazorStoreItem* item_ptr = SelectedItem();
		if (item_ptr == nullptr || busy) {
			return;
		}
		const RazorStoreItem item = *item_ptr;

		if (item.local_fallback_path.size() > 0 && item.download_url.isEmpty()) {
			SetBusyState(true, tr("Installing %1...").arg(item.name));
			progress_bar->setValue(65);
			QApplication::processEvents();
			const auto res = RazorStore::InstallPackageToDirectory(
			    item.local_fallback_path, TargetGamesDir(), item.title_id, item.name, item.sha256);
			FinishInstallAndRefresh(res, item.name);
			return;
		}

		// Resolve on-demand ProsperoStore API v1 record if loaded from compact index.json:
		if (item.download_url.startsWith(QStringLiteral("prospero-api://apps/"))) {
			const QString rel_path = item.download_url.mid(QStringLiteral("prospero-api://").size());
			const QUrl    api_url(QStringLiteral("https://prosperostore.net/api/v1/") + rel_path);
			SetBusyState(true, tr("Resolving release metadata for %1...").arg(item.name));
			progress_bar->setValue(5);

			QNetworkRequest req(api_url);
			req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("RazorEmulations/1.0"));
			QNetworkReply* reply = nam.get(req);
			connect(reply, &QNetworkReply::finished, this, [this, reply, item]() {
				reply->deleteLater();
				if (reply->error() != QNetworkReply::NoError) {
					SetBusyState(false, tr("Failed to resolve package URL: %1").arg(reply->errorString()));
					return;
				}
				const QJsonDocument doc = QJsonDocument::fromJson(reply->read(512 * 1024));
				const QJsonObject   obj = doc.object();
				const QString       artifact_url =
				    obj.value(QStringLiteral("artifact_url")).toString().trimmed();
				const QString sha = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
				if (artifact_url.isEmpty()) {
					SetBusyState(false, tr("No downloadable artifact URL found for %1.").arg(item.name));
					return;
				}
				StartMultiPartDownloadAndInstall({artifact_url}, {}, item.title_id, item.name,
				                                 item.format, sha, {});
			});
			return;
		}

		const QStringList urls =
		    !item.part_urls.isEmpty() ? item.part_urls : QStringList {item.download_url};
		StartMultiPartDownloadAndInstall(urls, item.part_names, item.title_id, item.name,
		                                 item.format, item.sha256, item.local_fallback_path);
	}

	void InstallFromDirectUrl() {
		if (busy) {
			return;
		}
		const QString raw_url = direct_url_edit->text().trimmed();
		const QUrl    url(raw_url);
		if (!url.isValid() ||
		    (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http") &&
		     !url.isLocalFile())) {
			QMessageBox::information(
			    dialog, tr("Direct URL Install"),
			    tr("Please enter a valid http:// or https:// URL pointing to a .zar, .zip, .7z, or .tar.gz PS5 package."));
			return;
		}

		if (url.isLocalFile()) {
			const QString local_path = url.toLocalFile();
			const auto    res        = RazorStore::InstallPackageToDirectory(
			    local_path, TargetGamesDir(), {}, QFileInfo(local_path).completeBaseName());
			FinishInstallAndRefresh(res, QFileInfo(local_path).fileName());
			return;
		}

		QString file_name = QFileInfo(url.path()).fileName();
		if (file_name.isEmpty()) {
			file_name = QStringLiteral("package.zar");
		}
		StartMultiPartDownloadAndInstall({raw_url}, {file_name}, {},
		                                 QFileInfo(file_name).completeBaseName(), file_name, {}, {});
	}

	void StartMultiPartDownloadAndInstall(const QStringList& urls, const QStringList& part_names,
	                                      const QString& title_id, const QString& display_name,
	                                      const QString& ext_hint, const QString& expected_sha256,
	                                      const QString& local_fallback_path) {
		if (urls.isEmpty()) {
			return;
		}
		const QString target_dir = TargetGamesDir();
		const QString dl_session_dir =
		    QDir(target_dir)
		        .filePath(QStringLiteral(".razor_dl_") + QUuid::createUuid().toString(QUuid::Id128));
		if (!QDir().mkpath(dl_session_dir)) {
			QMessageBox::warning(
			    dialog, tr("Download Error"),
			    tr("Could not create temporary download directory in %1").arg(target_dir));
			return;
		}

		DownloadPartAtIndex(0, urls, part_names, dl_session_dir, target_dir, title_id, display_name,
		                    ext_hint, expected_sha256, local_fallback_path);
	}

	void DownloadPartAtIndex(int part_idx, const QStringList& urls, const QStringList& part_names,
	                         const QString& dl_session_dir, const QString& target_dir,
	                         const QString& title_id, const QString& display_name,
	                         const QString& ext_hint, const QString& expected_sha256,
	                         const QString& local_fallback_path) {
		const QUrl    url(urls[part_idx]);
		const int     total_parts = urls.size();
		QString       part_file   = (part_idx < part_names.size()) ? part_names[part_idx].trimmed() : QString();
		if (part_file.isEmpty()) {
			part_file = QFileInfo(url.path()).fileName();
		}
		part_file = SanitizeFolderName(part_file);
		if (part_file.isEmpty() || !part_file.contains(QLatin1Char('.'))) {
			const QString fmt = DetectFormatFromUrlOrName(url.toString(), ext_hint);
			part_file = QStringLiteral("package_%1.%2").arg(part_idx + 1).arg(fmt);
		}

		const QString local_part_path = QDir(dl_session_dir).filePath(part_file);
		auto*         out_file        = new QFile(local_part_path);
		if (!out_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			delete out_file;
			QDir(dl_session_dir).removeRecursively();
			SetBusyState(false, tr("Could not write temporary file: %1").arg(local_part_path));
			return;
		}

		const QString part_caption =
		    total_parts > 1 ? tr(" (part %1/%2)").arg(part_idx + 1).arg(total_parts) : QString();
		SetBusyState(true,
		             tr("Downloading %1%2 from %3...").arg(display_name, part_caption, url.host()));
		progress_bar->setValue(qMax(5, (part_idx * 90) / qMax(1, total_parts)));

		QNetworkRequest req(url);
		req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("RazorEmulations/1.0"));
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		                 QNetworkRequest::NoLessSafeRedirectPolicy);
		QNetworkReply* reply = nam.get(req);
		out_file->setParent(reply);
		active_download = reply;

		connect(reply, &QNetworkReply::readyRead, this, [reply, out_file]() {
			out_file->write(reply->readAll());
		});

		connect(reply, &QNetworkReply::downloadProgress, this,
		        [this, display_name, part_caption, part_idx, total_parts](qint64 received,
		                                                                  qint64 total) {
			        if (total > 0) {
				        const double base_pct = (static_cast<double>(part_idx) * 90.0) / total_parts;
				        const double span_pct =
				            (static_cast<double>(received) * 90.0) / (total * total_parts);
				        progress_bar->setValue(qBound(5, static_cast<int>(base_pct + span_pct), 92));
				        status_label->setText(
				            tr("Downloading %1%2... %3 / %4")
				                .arg(display_name, part_caption,
				                     FormatByteSize(static_cast<uint64_t>(received)),
				                     FormatByteSize(static_cast<uint64_t>(total))));
			        }
		        });

		connect(reply, &QNetworkReply::finished, this,
		        [this, reply, out_file, part_idx, urls, part_names, dl_session_dir, local_part_path,
		         target_dir, title_id, display_name, ext_hint, expected_sha256,
		         local_fallback_path]() {
			        out_file->write(reply->readAll());
			        out_file->flush();
			        out_file->close();
			        const auto    net_err      = reply->error();
			        const QString err_str      = reply->errorString();
			        const QString content_type =
			            reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
			        const QUrl    orig_url     = reply->request().url();
			        reply->deleteLater();

			        if (net_err != QNetworkReply::NoError) {
				        QDir(dl_session_dir).removeRecursively();
				        if (!local_fallback_path.isEmpty() && QFileInfo::exists(local_fallback_path)) {
					        progress_bar->setValue(95);
					        status_label->setText(
					            tr("Offline / local cache hit — installing bundled %1...")
					                .arg(display_name));
					        QApplication::processEvents();
					        const auto fallback_res = RazorStore::InstallPackageToDirectory(
					            local_fallback_path, target_dir, title_id, display_name,
					            expected_sha256);
					        FinishInstallAndRefresh(fallback_res, display_name);
					        return;
				        }

				        SetBusyState(false, tr("Download failed: %1").arg(err_str));
				        QMessageBox::warning(dialog, tr("Download Failed"),
				                             tr("Could not download %1:\n%2").arg(display_name, err_str));
				        return;
			        }

			        // Detect CAPTCHA / file-locker browser pages (common in 3rd-party ps5-library mirrors):
			        if (content_type.contains(QStringLiteral("text/html")) ||
			            LooksLikeHtmlPage(local_part_path)) {
				        QDir(dl_session_dir).removeRecursively();
				        SetBusyState(
				            false,
				            tr("Mirror requires browser verification (CAPTCHA) — open in browser or choose another source."));
				        const auto answer = QMessageBox::question(
				            dialog, tr("Browser Verification Required"),
				            tr("\"%1\" is hosted on a file host (%2) that requires interactive browser verification (CAPTCHA).\n\n"
				               "Open this link in your web browser now?\n"
				               "Once downloaded, click \"Import Local Package...\" in Razor Store to auto-install into Games/.")
				                .arg(display_name, orig_url.host()),
				            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
				        if (answer == QMessageBox::Yes) {
					        QDesktopServices::openUrl(orig_url);
				        }
				        return;
			        }

			        if (part_idx + 1 < urls.size()) {
				        DownloadPartAtIndex(part_idx + 1, urls, part_names, dl_session_dir,
				                            target_dir, title_id, display_name, ext_hint,
				                            expected_sha256, local_fallback_path);
				        return;
			        }

			        // All parts downloaded — verify & auto-install from the first part!
			        const QDir        sess_dir(dl_session_dir);
			        const QStringList downloaded_files =
			            sess_dir.entryList(QDir::Files, QDir::Name);
			        const QString primary_file =
			            !downloaded_files.isEmpty()
			                ? sess_dir.filePath(downloaded_files.first())
			                : local_part_path;

			        progress_bar->setValue(95);
			        status_label->setText(
			            tr("Verifying && installing %1 into Games folder...").arg(display_name));
			        QApplication::processEvents();

			        const QString sha_check = (urls.size() == 1) ? expected_sha256 : QString();
			        const auto    res       = RazorStore::InstallPackageToDirectory(
                        primary_file, target_dir, title_id, display_name, sha_check);
			        QDir(dl_session_dir).removeRecursively();
			        FinishInstallAndRefresh(res, display_name);
		        });
	}

	void ImportLocalPackage() {
		if (busy) {
			return;
		}
		const QString path = QFileDialog::getOpenFileName(
		    dialog, tr("Import PS5 Package (.zar, .zip, .7z, .tar.gz, .elf)"), {},
		    tr("PS5 Packages (*.zar *.zip *.7z *.tar.gz *.tgz *.tar *.rar *.elf *.bin);;All Files (*)"));
		if (path.isEmpty()) {
			return;
		}
		SetBusyState(true, tr("Installing %1...").arg(QFileInfo(path).fileName()));
		progress_bar->setValue(50);
		QApplication::processEvents();

		const auto res = RazorStore::InstallPackageToDirectory(
		    path, TargetGamesDir(), {}, QFileInfo(path).completeBaseName());
		FinishInstallAndRefresh(res, QFileInfo(path).fileName());
	}

	void LaunchSelectedItem() {
		const RazorStoreItem* item = SelectedItem();
		if (item == nullptr || list_widget == nullptr) {
			return;
		}
		if (list_widget->SelectGameByTitleIdOrPath(item->title_id, {})) {
			emit dialog->LaunchRequested();
			dialog->accept();
		}
	}
};

RazorStoreDialog::RazorStoreDialog(IRazorStoreLibrary* library, const QString& settings_file,
                                   QWidget* parent)
    : QDialog(parent), m_p(new Private(this)) {
	m_p->list_widget   = library;
	m_p->settings_file = settings_file;
	m_p->SetupUi();
}

RazorStoreDialog::~RazorStoreDialog() = default;

#include "razorStoreDialog.moc"
