#include "razorStoreDialog.h"

#include "common/archive.h"
#include "gameContent.h"

#include <QAbstractItemView>
#include <QApplication>
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
#include <QSettings>
#include <QSize>
#include <QSplitter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <utility>

namespace {

constexpr char OFFICIAL_CATALOG_URL[] =
    "https://raw.githubusercontent.com/RazorCz36/Razor-emulation/main/store/catalog.json";
constexpr char SETTINGS_STORE_GROUP[]     = "RazorStore";
constexpr char SETTINGS_CUSTOM_CATALOGS[] = "custom_catalog_urls";
constexpr qint64 MAX_CATALOG_BYTES        = 4LL * 1024LL * 1024LL;

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

bool ValidateArchiveEntriesSafe(const QString& tar_exe, const QString& archive_path,
                                QString* out_error) {
	QProcess list_proc;
	list_proc.start(tar_exe, {QStringLiteral("-tf"), archive_path});
	if (!list_proc.waitForFinished(30000) || list_proc.exitStatus() != QProcess::NormalExit ||
	    list_proc.exitCode() != 0) {
		if (out_error != nullptr) {
			*out_error = QObject::tr("Archive header inspection failed (corrupt or unsupported archive).");
		}
		return false;
	}

	const QString listing = QString::fromUtf8(list_proc.readAllStandardOutput());
	const auto    lines   = listing.split(QRegularExpression(QStringLiteral("[\r\n]+")),
	                                      Qt::SkipEmptyParts);
	if (lines.isEmpty()) {
		if (out_error != nullptr) {
			*out_error = QObject::tr("Archive is empty.");
		}
		return false;
	}

	for (const QString& raw_entry: lines) {
		QString entry = raw_entry;
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

QString ReadTitleIdFromParamJson(const QByteArray& param_bytes) {
	const QJsonDocument doc = QJsonDocument::fromJson(param_bytes);
	if (!doc.isObject()) {
		return {};
	}
	return doc.object().value(QStringLiteral("titleId")).toString().trimmed().toUpper();
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
	           title_id.isEmpty() ? QStringLiteral("PS5") : title_id);

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

bool ParseCatalogJson(const QByteArray& json_bytes, const QString& base_dir,
                      QString* out_store_name, QVector<RazorStoreItem>* out_items,
                      QString* out_error) {
	QJsonParseError parse_err {};
	const QJsonDocument doc = QJsonDocument::fromJson(json_bytes, &parse_err);
	if (parse_err.error != QJsonParseError::NoError || !doc.isObject()) {
		if (out_error != nullptr) {
			*out_error = QObject::tr("Invalid catalog JSON: %1").arg(parse_err.errorString());
		}
		return false;
	}

	const QJsonObject root       = doc.object();
	const QString     store_name = root.value(QStringLiteral("store_name"))
	                                   .toString(QObject::tr("Razor Store Catalog"))
	                                   .trimmed();
	if (out_store_name != nullptr) {
		*out_store_name = store_name;
	}

	const QJsonArray items_arr = root.value(QStringLiteral("items")).toArray();
	QVector<RazorStoreItem> parsed;
	parsed.reserve(items_arr.size());

	for (const QJsonValue& val: items_arr) {
		if (!val.isObject()) {
			continue;
		}
		const QJsonObject obj = val.toObject();
		RazorStoreItem    item;
		item.title_id    = obj.value(QStringLiteral("title_id")).toString().trimmed().toUpper();
		item.name        = obj.value(QStringLiteral("name")).toString().trimmed();
		item.subtitle    = obj.value(QStringLiteral("subtitle")).toString().trimmed();
		item.category    = obj.value(QStringLiteral("category")).toString(QStringLiteral("Game")).trimmed();
		item.version     = obj.value(QStringLiteral("version")).toString(QStringLiteral("01.00")).trimmed();
		item.author      = obj.value(QStringLiteral("author")).toString(QStringLiteral("Community")).trimmed();
		item.license     = obj.value(QStringLiteral("license")).toString(QStringLiteral("Open Homebrew")).trimmed();
		item.resolutions = obj.value(QStringLiteral("resolutions"))
		                       .toString(QStringLiteral("1080p FHD · 1440p WQHD · 4K UHD"))
		                       .trimmed();
		item.controls    = obj.value(QStringLiteral("controls"))
		                       .toString(QStringLiteral("DualSense Controller or Mapped PC Keyboard"))
		                       .trimmed();
		item.description = obj.value(QStringLiteral("description")).toString().trimmed();
		item.format      = obj.value(QStringLiteral("format")).toString(QStringLiteral("zar")).trimmed().toLower();
		item.size_bytes  = static_cast<uint64_t>(obj.value(QStringLiteral("size_bytes")).toInteger(0));
		item.sha256      = obj.value(QStringLiteral("sha256")).toString().trimmed().toLower();
		item.download_url = obj.value(QStringLiteral("download_url")).toString().trimmed();
		item.icon_url     = obj.value(QStringLiteral("icon_url")).toString().trimmed();
		item.source_name  = store_name;

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

		if (item.name.isEmpty() || (item.download_url.isEmpty() && item.local_fallback_path.isEmpty())) {
			continue;
		}
		parsed.push_back(std::move(item));
	}

	if (out_items != nullptr) {
		*out_items = std::move(parsed);
	}
	return true;
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

	if (!expected_sha256.trimmed().isEmpty()) {
		const QString actual_sha = ComputeFileSha256(source_package_path);
		if (actual_sha.compare(expected_sha256.trimmed(), Qt::CaseInsensitive) != 0) {
			result.error_message = QObject::tr("SHA-256 verification failed.\nExpected: %1\nActual: %2")
			                           .arg(expected_sha256.trimmed().toLower(), actual_sha);
			return result;
		}
	}

	const QString lower_name = src_info.fileName().toLower();

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

	// Case 2: Archive (.zip, .tar.gz, .tgz, .tar, .7z) extracted safely via system tar
	if (lower_name.endsWith(QStringLiteral(".zip")) ||
	    lower_name.endsWith(QStringLiteral(".tar.gz")) ||
	    lower_name.endsWith(QStringLiteral(".tgz")) ||
	    lower_name.endsWith(QStringLiteral(".tar"))) {
		const QString tar_exe = ResolveSystemTar();
		if (tar_exe.isEmpty()) {
			result.error_message = QObject::tr("System tar extractor was not found on this OS.");
			return result;
		}

		QString safety_err;
		if (!ValidateArchiveEntriesSafe(tar_exe, source_package_path, &safety_err)) {
			result.error_message = safety_err;
			return result;
		}

		const QString staging_path = QDir(target_games_dir)
		                                 .filePath(QStringLiteral(".razor_stage_") +
		                                           QUuid::createUuid().toString(QUuid::Id128));
		if (!QDir().mkpath(staging_path)) {
			result.error_message = QObject::tr("Could not create temporary staging folder.");
			return result;
		}

		const auto cleanup_staging = [&staging_path]() { QDir(staging_path).removeRecursively(); };

		QProcess extract_proc;
		extract_proc.start(tar_exe,
		                   {QStringLiteral("-xf"), source_package_path, QStringLiteral("-C"), staging_path});
		if (!extract_proc.waitForFinished(180000) ||
		    extract_proc.exitStatus() != QProcess::NormalExit || extract_proc.exitCode() != 0) {
			const QString err_out = QString::fromUtf8(extract_proc.readAllStandardError()).trimmed();
			cleanup_staging();
			result.error_message =
			    QObject::tr("Archive extraction failed: %1")
			        .arg(err_out.isEmpty() ? QObject::tr("unknown tar error") : err_out);
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
		QString found_game_root;
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
			    "Archive does not contain a PS5 game (missing eboot.bin or .zar package).");
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
	    "Unsupported file type (%1). Supported formats: .zar, .zip, .tar.gz, .elf, .bin.")
	                           .arg(src_info.fileName());
	return result;
}

} // namespace RazorStore

class RazorStoreDialog::Private: public QObject {
	Q_OBJECT

public:
	RazorStoreDialog*     dialog        = nullptr;
	IRazorStoreLibrary*   list_widget   = nullptr;
	QString               settings_file;
	QNetworkAccessManager    nam;

	QLabel*       install_dir_label   = nullptr;
	QLineEdit*    search_edit         = nullptr;
	QComboBox*    category_combo      = nullptr;
	QListWidget*  catalog_list        = nullptr;

	QLabel*       cover_label         = nullptr;
	QLabel*       title_label         = nullptr;
	QLabel*       subtitle_label      = nullptr;
	QLabel*       badges_label        = nullptr;
	QLabel*       meta_label          = nullptr;
	QLabel*       controls_label      = nullptr;
	QLabel*       description_label   = nullptr;
	QProgressBar* progress_bar        = nullptr;
	QLabel*       status_label        = nullptr;
	QPushButton*  install_button      = nullptr;
	QPushButton*  launch_button       = nullptr;

	QLineEdit*    direct_url_edit     = nullptr;
	QPushButton*  direct_url_button   = nullptr;
	QPushButton*  local_import_button = nullptr;

	QVector<RazorStoreItem>      all_items;
	QVector<int>                 filtered_indices;
	QMap<QString, QPixmap>       icon_cache;
	QStringList                  custom_catalog_urls;
	QPointer<QNetworkReply>      active_download;
	bool                         busy = false;

	explicit Private(RazorStoreDialog* owner): QObject(owner), dialog(owner) {}

	void SetupUi() {
		dialog->setWindowTitle(tr("Razor Store — PS5 Homebrew, Tech Demos & Package Hub"));
		dialog->resize(1040, 660);
		dialog->setMinimumSize(860, 560);

		auto* root = new QVBoxLayout(dialog);
		root->setContentsMargins(14, 14, 14, 14);
		root->setSpacing(10);

		// 1. Luxury Gunmetal Command Banner
		auto* header_frame = new QFrame(dialog);
		header_frame->setObjectName(QStringLiteral("razor_header_frame"));
		auto* header_layout = new QHBoxLayout(header_frame);
		header_layout->setContentsMargins(14, 10, 14, 10);
		header_layout->setSpacing(12);

		auto* store_logo_label = new QLabel(header_frame);
		store_logo_label->setFixedSize(44, 44);
		const QIcon store_brand_icon(QStringLiteral(":/icons/razor-logo.png"));
		store_logo_label->setPixmap(
		    store_brand_icon.pixmap(QSize(44, 44), dialog->devicePixelRatioF()));
		header_layout->addWidget(store_logo_label);

		auto* title_col = new QVBoxLayout();
		title_col->setSpacing(2);
		auto* store_title = new QLabel(tr("✦  RAZOR STORE  ·  DIRECT AUTO-INSTALL HUB"), header_frame);
		store_title->setObjectName(QStringLiteral("brand_title_label"));
		install_dir_label = new QLabel(header_frame);
		install_dir_label->setObjectName(QStringLiteral("resolution_hint_label"));
		title_col->addWidget(store_title);
		title_col->addWidget(install_dir_label);
		header_layout->addLayout(title_col, 1);

		auto* open_dir_btn = new QPushButton(tr("Open Games Folder"), header_frame);
		open_dir_btn->setCursor(Qt::PointingHandCursor);
		auto* add_source_btn = new QPushButton(tr("+ Custom Store URL"), header_frame);
		add_source_btn->setCursor(Qt::PointingHandCursor);
		auto* refresh_btn = new QPushButton(tr("↻ Refresh Store"), header_frame);
		refresh_btn->setCursor(Qt::PointingHandCursor);

		header_layout->addWidget(open_dir_btn);
		header_layout->addWidget(add_source_btn);
		header_layout->addWidget(refresh_btn);
		root->addWidget(header_frame);

		// 2. Search & Category Filter Row
		auto* filter_row = new QHBoxLayout();
		filter_row->setSpacing(10);
		search_edit = new QLineEdit(dialog);
		search_edit->setPlaceholderText(
		    tr("Search PS5 homebrew games, benchmarks, PPSA title IDs, or tags..."));
		search_edit->setClearButtonEnabled(true);

		category_combo = new QComboBox(dialog);
		category_combo->setMinimumWidth(210);
		category_combo->addItem(tr("All Store Titles"));
		category_combo->addItem(tr("Games"));
		category_combo->addItem(tr("Demos & Diagnostics"));
		category_combo->addItem(tr("Installed in Library"));

		filter_row->addWidget(search_edit, 1);
		filter_row->addWidget(category_combo);
		root->addLayout(filter_row);

		// 3. Split Catalog List + Rich Game Inspector
		auto* splitter = new QSplitter(Qt::Horizontal, dialog);
		splitter->setChildrenCollapsible(false);

		catalog_list = new QListWidget(splitter);
		catalog_list->setIconSize(QSize(56, 56));
		catalog_list->setSpacing(4);
		catalog_list->setAlternatingRowColors(true);
		catalog_list->setSelectionMode(QAbstractItemView::SingleSelection);
		catalog_list->setMinimumWidth(370);

		auto* inspector_frame = new QFrame(splitter);
		inspector_frame->setObjectName(QStringLiteral("razor_footer_frame"));
		auto* inspector_layout = new QVBoxLayout(inspector_frame);
		inspector_layout->setContentsMargins(18, 16, 18, 16);
		inspector_layout->setSpacing(10);

		auto* hero_row = new QHBoxLayout();
		hero_row->setSpacing(16);

		cover_label = new QLabel(inspector_frame);
		cover_label->setFixedSize(136, 136);
		cover_label->setAlignment(Qt::AlignCenter);
		hero_row->addWidget(cover_label, 0, Qt::AlignTop);

		auto* hero_meta = new QVBoxLayout();
		hero_meta->setSpacing(4);
		title_label = new QLabel(tr("Select a Title"), inspector_frame);
		title_label->setStyleSheet(QStringLiteral("font-size: 19px; font-weight: 700;"));
		title_label->setWordWrap(true);

		subtitle_label = new QLabel(inspector_frame);
		subtitle_label->setObjectName(QStringLiteral("resolution_hint_label"));
		subtitle_label->setWordWrap(true);

		badges_label = new QLabel(inspector_frame);
		badges_label->setWordWrap(true);

		meta_label = new QLabel(inspector_frame);
		meta_label->setObjectName(QStringLiteral("brand_subtitle_label"));
		meta_label->setWordWrap(true);

		hero_meta->addWidget(title_label);
		hero_meta->addWidget(subtitle_label);
		hero_meta->addWidget(badges_label);
		hero_meta->addWidget(meta_label);
		hero_meta->addStretch(1);
		hero_row->addLayout(hero_meta, 1);
		inspector_layout->addLayout(hero_row);

		controls_label = new QLabel(inspector_frame);
		controls_label->setWordWrap(true);
		controls_label->setStyleSheet(
		    QStringLiteral("padding: 8px 10px; border: 1px solid palette(mid); border-radius: 6px;"));
		inspector_layout->addWidget(controls_label);

		description_label = new QLabel(inspector_frame);
		description_label->setWordWrap(true);
		description_label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
		inspector_layout->addWidget(description_label, 1);

		progress_bar = new QProgressBar(inspector_frame);
		progress_bar->setRange(0, 100);
		progress_bar->setValue(0);
		progress_bar->setTextVisible(true);
		progress_bar->setVisible(false);
		inspector_layout->addWidget(progress_bar);

		status_label = new QLabel(
		    tr("Ready — games download and install directly into your configured Games folder."),
		    inspector_frame);
		status_label->setObjectName(QStringLiteral("brand_subtitle_label"));
		status_label->setWordWrap(true);
		inspector_layout->addWidget(status_label);

		auto* action_row = new QHBoxLayout();
		action_row->setSpacing(10);
		install_button = new QPushButton(tr("⬇  DOWNLOAD && AUTO-INSTALL"), inspector_frame);
		install_button->setObjectName(QStringLiteral("launch_game_button"));
		install_button->setMinimumHeight(40);
		install_button->setCursor(Qt::PointingHandCursor);

		launch_button = new QPushButton(tr("▶  LAUNCH GAME NOW"), inspector_frame);
		launch_button->setObjectName(QStringLiteral("razor_store_button"));
		launch_button->setMinimumHeight(40);
		launch_button->setCursor(Qt::PointingHandCursor);

		action_row->addWidget(install_button, 1);
		action_row->addWidget(launch_button, 1);
		inspector_layout->addLayout(action_row);

		splitter->addWidget(catalog_list);
		splitter->addWidget(inspector_frame);
		splitter->setStretchFactor(0, 4);
		splitter->setStretchFactor(1, 6);
		root->addWidget(splitter, 1);

		// 4. Bottom Direct Package URL / Local Archive Bar
		auto* direct_frame = new QFrame(dialog);
		direct_frame->setObjectName(QStringLiteral("razor_footer_frame"));
		auto* direct_layout = new QHBoxLayout(direct_frame);
		direct_layout->setContentsMargins(12, 8, 12, 8);
		direct_layout->setSpacing(8);

		auto* direct_caption = new QLabel(tr("Direct Install:"), direct_frame);
		direct_caption->setObjectName(QStringLiteral("pc_resolution_caption"));

		direct_url_edit = new QLineEdit(direct_frame);
		direct_url_edit->setPlaceholderText(
		    tr("Paste direct .zar / .zip / .tar.gz package URL for automatic download & extraction..."));

		direct_url_button = new QPushButton(tr("⬇ Install URL"), direct_frame);
		direct_url_button->setCursor(Qt::PointingHandCursor);

		local_import_button = new QPushButton(tr("Import Local .zar / .zip..."), direct_frame);
		local_import_button->setCursor(Qt::PointingHandCursor);

		direct_layout->addWidget(direct_caption);
		direct_layout->addWidget(direct_url_edit, 1);
		direct_layout->addWidget(direct_url_button);
		direct_layout->addWidget(local_import_button);
		root->addWidget(direct_frame);

		// Connections
		connect(open_dir_btn, &QPushButton::clicked, this, [this]() {
			const QString dir = TargetGamesDir();
			QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
		});
		connect(add_source_btn, &QPushButton::clicked, this, &Private::PromptCustomCatalogUrl);
		connect(refresh_btn, &QPushButton::clicked, this, &Private::ReloadCatalogs);
		connect(search_edit, &QLineEdit::textChanged, this, [this]() { RebuildList(); });
		connect(category_combo, &QComboBox::currentIndexChanged, this, [this]() { RebuildList(); });
		connect(catalog_list, &QListWidget::currentRowChanged, this,
		        [this](int) { UpdateInspector(); });
		connect(install_button, &QPushButton::clicked, this, &Private::InstallSelectedItem);
		connect(launch_button, &QPushButton::clicked, this, &Private::LaunchSelectedItem);
		connect(direct_url_button, &QPushButton::clicked, this, &Private::InstallFromDirectUrl);
		connect(local_import_button, &QPushButton::clicked, this, &Private::ImportLocalPackage);

		LoadCustomCatalogSettings();
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
		install_dir_label->setText(
		    tr("Auto-Install Target Folder: %1  (Zero manual setup — ready immediately)")
		        .arg(QDir::toNativeSeparators(TargetGamesDir())));
	}

	void LoadCustomCatalogSettings() {
		if (settings_file.isEmpty()) {
			return;
		}
		QSettings s(settings_file, QSettings::IniFormat);
		s.beginGroup(SETTINGS_STORE_GROUP);
		custom_catalog_urls = s.value(SETTINGS_CUSTOM_CATALOGS).toStringList();
		custom_catalog_urls.removeAll(QString());
		s.endGroup();
	}

	void SaveCustomCatalogSettings() const {
		if (settings_file.isEmpty()) {
			return;
		}
		QSettings s(settings_file, QSettings::IniFormat);
		s.beginGroup(SETTINGS_STORE_GROUP);
		s.setValue(SETTINGS_CUSTOM_CATALOGS, custom_catalog_urls);
		s.endGroup();
	}

	void PromptCustomCatalogUrl() {
		bool          ok      = false;
		const QString current = custom_catalog_urls.join(QStringLiteral("; "));
		const QString input   = QInputDialog::getText(
		    dialog, tr("Custom Store Catalog URLs"),
		    tr("Enter one or more catalog.json URLs (separated by ';'), e.g. from your LAN NAS\n"
		         "or store/host_private_store.ps1 (leave blank to reset to Official Store only):"),
		    QLineEdit::Normal, current, &ok);
		if (!ok) {
			return;
		}

		QStringList updated;
		for (const QString& part: input.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
			const QString trimmed = part.trimmed();
			const QUrl    url(trimmed);
			if (url.isValid() &&
			    (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http") ||
			     url.isLocalFile())) {
				updated.push_back(trimmed);
			}
		}
		updated.removeDuplicates();
		custom_catalog_urls = updated;
		SaveCustomCatalogSettings();
		ReloadCatalogs();
	}

	void MergeCatalogItems(const QVector<RazorStoreItem>& incoming) {
		for (const RazorStoreItem& item: incoming) {
			int existing_idx = -1;
			for (int i = 0; i < all_items.size(); ++i) {
				if (!item.title_id.isEmpty() && all_items[i].title_id == item.title_id) {
					existing_idx = i;
					break;
				}
			}
			if (existing_idx >= 0) {
				// Preserve local fallback/icon paths from bundled store when remote catalog updates metadata:
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

		RebuildList();

		// 2. Fetch live Official GitHub catalog + any user-configured Custom Catalog URLs asynchronously
		QStringList urls_to_fetch {QString::fromLatin1(OFFICIAL_CATALOG_URL)};
		for (const QString& custom_url: std::as_const(custom_catalog_urls)) {
			if (!urls_to_fetch.contains(custom_url)) {
				urls_to_fetch.push_back(custom_url);
			}
		}

		for (const QString& url_str: urls_to_fetch) {
			const QUrl url(url_str);
			if (url.isLocalFile()) {
				const QFileInfo fi(url.toLocalFile());
				QFile           f(fi.absoluteFilePath());
				if (f.open(QIODevice::ReadOnly)) {
					QString                 name;
					QVector<RazorStoreItem> items;
					if (RazorStore::ParseCatalogJson(f.readAll(), fi.absolutePath(), &name, &items)) {
						MergeCatalogItems(items);
						RebuildList();
					}
				}
				continue;
			}

			QNetworkRequest req(url);
			req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
			                 QNetworkRequest::NoLessSafeRedirectPolicy);
			req.setTransferTimeout(12000);
			QNetworkReply* reply = nam.get(req);
			connect(reply, &QNetworkReply::finished, this, [this, reply, bundled_dir]() {
				reply->deleteLater();
				if (reply->error() != QNetworkReply::NoError) {
					return;
				}
				const QByteArray payload = reply->read(MAX_CATALOG_BYTES);
				QString                 name;
				QVector<RazorStoreItem> remote_items;
				if (RazorStore::ParseCatalogJson(payload, bundled_dir, &name, &remote_items)) {
					MergeCatalogItems(remote_items);
					RebuildList();
				}
			});
		}
	}

	QPixmap IconForItem(const RazorStoreItem& item, int px) {
		const QString key = QStringLiteral("%1_%2").arg(item.title_id, QString::number(px));
		if (icon_cache.contains(key)) {
			return icon_cache.value(key);
		}
		if (!item.local_icon_path.isEmpty() && QFileInfo::exists(item.local_icon_path)) {
			QPixmap loaded(item.local_icon_path);
			if (!loaded.isNull()) {
				const QPixmap scaled =
				    loaded.scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
				icon_cache.insert(key, scaled);
				return scaled;
			}
		}

		if (!item.icon_url.isEmpty()) {
			const QUrl url(item.icon_url);
			if (url.isValid() &&
			    (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))) {
				QNetworkRequest req(url);
				req.setTransferTimeout(10000);
				QNetworkReply* reply = nam.get(req);
				const QString  tid   = item.title_id;
				connect(reply, &QNetworkReply::finished, this, [this, reply, tid]() {
					reply->deleteLater();
					if (reply->error() != QNetworkReply::NoError) {
						return;
					}
					QPixmap net_pix;
					if (net_pix.loadFromData(reply->read(2 * 1024 * 1024))) {
						icon_cache.insert(tid + QStringLiteral("_56"),
						                  net_pix.scaled(56, 56, Qt::KeepAspectRatio,
						                                 Qt::SmoothTransformation));
						icon_cache.insert(tid + QStringLiteral("_136"),
						                  net_pix.scaled(136, 136, Qt::KeepAspectRatio,
						                                 Qt::SmoothTransformation));
						RebuildList();
					}
				});
			}
		}

		return MakeFallbackCoverPixmap(item.name, item.title_id, px);
	}

	void RebuildList() {
		const QString selected_tid = SelectedItem() != nullptr ? SelectedItem()->title_id : QString();
		const QMap<QString, QString> installed_map =
		    list_widget != nullptr ? list_widget->GetInstalledTitleVersions()
		                           : QMap<QString, QString> {};
		const QString query        = search_edit->text().trimmed();
		const int     cat_filter   = category_combo->currentIndex();

		catalog_list->blockSignals(true);
		catalog_list->clear();
		filtered_indices.clear();

		int row_to_select = 0;
		for (int i = 0; i < all_items.size(); ++i) {
			const RazorStoreItem& item         = all_items[i];
			const bool            is_installed = installed_map.contains(item.title_id);

			if (cat_filter == 1 &&
			    item.category.contains(QStringLiteral("Diagnostic"), Qt::CaseInsensitive)) {
				continue;
			}
			if (cat_filter == 2 &&
			    !item.category.contains(QStringLiteral("Diagnostic"), Qt::CaseInsensitive) &&
			    !item.category.contains(QStringLiteral("Demo"), Qt::CaseInsensitive)) {
				continue;
			}
			if (cat_filter == 3 && !is_installed) {
				continue;
			}

			if (!query.isEmpty()) {
				const bool match = item.name.contains(query, Qt::CaseInsensitive) ||
				                   item.title_id.contains(query, Qt::CaseInsensitive) ||
				                   item.category.contains(query, Qt::CaseInsensitive) ||
				                   item.description.contains(query, Qt::CaseInsensitive);
				if (!match) {
					continue;
				}
			}

			const QString status_tag =
			    is_installed ? tr("  [INSTALLED ✓]") : QStringLiteral("  [%1]").arg(FormatByteSize(item.size_bytes));
			const QString label = QStringLiteral("%1%2\n%3  ·  %4  ·  v%5")
			                          .arg(item.name, status_tag, item.title_id, item.category,
			                               item.version);

			auto* list_item = new QListWidgetItem(QIcon(IconForItem(item, 56)), label);
			list_item->setSizeHint(QSize(320, 68));
			if (is_installed) {
				list_item->setForeground(QColor(0xD6, 0xB2, 0x6C));
			}
			catalog_list->addItem(list_item);
			filtered_indices.push_back(i);

			if (!selected_tid.isEmpty() && item.title_id == selected_tid) {
				row_to_select = filtered_indices.size() - 1;
			}
		}

		if (catalog_list->count() > 0) {
			catalog_list->setCurrentRow(row_to_select);
		}
		catalog_list->blockSignals(false);
		UpdateInspector();
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
			title_label->setText(tr("No Store Title Selected"));
			subtitle_label->setText(
			    tr("Use the search filter above or paste a direct .zar / .zip URL below."));
			badges_label->clear();
			meta_label->clear();
			controls_label->setText(tr("Controls: DualSense Controller or Mapped PC Keyboard"));
			description_label->clear();
			install_button->setEnabled(false);
			launch_button->setEnabled(false);
			return;
		}

		const QMap<QString, QString> installed_map =
		    list_widget != nullptr ? list_widget->GetInstalledTitleVersions()
		                           : QMap<QString, QString> {};
		const bool is_installed = installed_map.contains(item->title_id);

		cover_label->setPixmap(IconForItem(*item, 136));
		title_label->setText(item->name);
		subtitle_label->setText(item->subtitle);
		badges_label->setText(
		    QStringLiteral("<b>%1</b> &nbsp;·&nbsp; %2 &nbsp;·&nbsp; %3")
		        .arg(item->title_id.toHtmlEscaped(), item->resolutions.toHtmlEscaped(),
		             is_installed ? tr("<span style='color:#63D297;'>INSTALLED IN LIBRARY ✓</span>")
		                          : tr("Ready to Install")));
		meta_label->setText(
		    tr("Version %1  ·  Package: .%2 (%3)  ·  Author: %4  ·  License: %5")
		        .arg(item->version, item->format.toUpper(), FormatByteSize(item->size_bytes),
		             item->author, item->license));
		controls_label->setText(tr("◆  Controls: %1").arg(item->controls));
		description_label->setText(item->description);

		install_button->setEnabled(!busy);
		install_button->setText(is_installed ? tr("↻  REINSTALL / VERIFY PACKAGE")
		                                     : tr("⬇  DOWNLOAD && AUTO-INSTALL"));
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

		// Fast path: if bundled local package exists and matches expected SHA-256, install directly
		// (or fall back automatically if remote download fails / user is offline).
		if (item.download_url.isEmpty() && !item.local_fallback_path.isEmpty()) {
			SetBusyState(true, tr("Installing %1...").arg(item.name));
			progress_bar->setValue(65);
			QApplication::processEvents();
			const auto res = RazorStore::InstallPackageToDirectory(
			    item.local_fallback_path, TargetGamesDir(), item.title_id, item.name, item.sha256);
			FinishInstallAndRefresh(res, item.name);
			return;
		}

		StartRemoteDownloadAndInstall(QUrl(item.download_url), item.title_id, item.name,
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
			    tr("Please enter a valid http:// or https:// URL pointing to a .zar, .zip, or .tar.gz PS5 package."));
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
		StartRemoteDownloadAndInstall(url, {}, QFileInfo(file_name).completeBaseName(),
		                              file_name, {}, {});
	}

	void StartRemoteDownloadAndInstall(const QUrl& url, const QString& title_id,
	                                   const QString& display_name, const QString& ext_hint,
	                                   const QString& expected_sha256,
	                                   const QString& local_fallback_path) {
		const QString target_dir = TargetGamesDir();
		QString       suffix     = QStringLiteral(".zar");
		const QString lower_hint = (url.path() + QLatin1Char(' ') + ext_hint).toLower();
		if (lower_hint.contains(QStringLiteral(".tar.gz")) ||
		    lower_hint.contains(QStringLiteral(".tgz"))) {
			suffix = QStringLiteral(".tar.gz");
		} else if (lower_hint.contains(QStringLiteral(".zip"))) {
			suffix = QStringLiteral(".zip");
		} else if (lower_hint.contains(QStringLiteral(".elf"))) {
			suffix = QStringLiteral(".elf");
		} else if (lower_hint.contains(QStringLiteral(".bin"))) {
			suffix = QStringLiteral(".bin");
		}

		const QString temp_download = QDir(target_dir)
		                                  .filePath(QStringLiteral(".razor_dl_") +
		                                            QUuid::createUuid().toString(QUuid::Id128) + suffix);

		auto* out_file = new QFile(temp_download);
		if (!out_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			delete out_file;
			QMessageBox::warning(dialog, tr("Download Error"),
			                     tr("Could not create temporary download file in %1").arg(target_dir));
			return;
		}

		SetBusyState(true, tr("Downloading %1 from %2...").arg(display_name, url.host()));
		progress_bar->setValue(5);

		QNetworkRequest req(url);
		req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
		                 QNetworkRequest::NoLessSafeRedirectPolicy);
		QNetworkReply* reply = nam.get(req);
		out_file->setParent(reply);
		active_download = reply;

		connect(reply, &QNetworkReply::readyRead, this, [reply, out_file]() {
			out_file->write(reply->readAll());
		});

		connect(reply, &QNetworkReply::downloadProgress, this,
		        [this, display_name](qint64 received, qint64 total) {
			        if (total > 0) {
				        const int pct = static_cast<int>((received * 90LL) / total);
				        progress_bar->setValue(qBound(5, pct, 92));
				        status_label->setText(
				            tr("Downloading %1... %2 / %3")
				                .arg(display_name, FormatByteSize(static_cast<uint64_t>(received)),
				                     FormatByteSize(static_cast<uint64_t>(total))));
			        }
		        });

		connect(reply, &QNetworkReply::finished, this,
		        [this, reply, out_file, temp_download, target_dir, title_id, display_name,
		         expected_sha256, local_fallback_path]() {
			        out_file->write(reply->readAll());
			        out_file->flush();
			        out_file->close();
			        const auto net_err = reply->error();
			        const QString err_str = reply->errorString();
			        reply->deleteLater();

			        if (net_err != QNetworkReply::NoError) {
				        QFile::remove(temp_download);
				        // Seamless offline / pre-push fallback when bundled package is present!
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

			        progress_bar->setValue(95);
			        status_label->setText(tr("Verifying & installing %1 into Games folder...")
			                                  .arg(display_name));
			        QApplication::processEvents();

			        const auto res = RazorStore::InstallPackageToDirectory(
			            temp_download, target_dir, title_id, display_name, expected_sha256);
			        QFile::remove(temp_download);
			        FinishInstallAndRefresh(res, display_name);
		        });
	}

	void ImportLocalPackage() {
		if (busy) {
			return;
		}
		const QString path = QFileDialog::getOpenFileName(
		    dialog, tr("Import PS5 Package (.zar, .zip, .tar.gz, .elf)"), {},
		    tr("PS5 Packages (*.zar *.zip *.tar.gz *.tgz *.elf *.bin);;All Files (*)"));
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
