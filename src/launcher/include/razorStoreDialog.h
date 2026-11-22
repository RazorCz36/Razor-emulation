#ifndef RAZOR_STORE_DIALOG_H
#define RAZOR_STORE_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

class QWidget;

class IRazorStoreLibrary {
public:
	virtual ~IRazorStoreLibrary()                                                    = default;
	[[nodiscard]] virtual QString                GetPrimaryInstallDirectory()        = 0;
	[[nodiscard]] virtual QMap<QString, QString> GetInstalledTitleVersions() const   = 0;
	virtual void                                 ScanGameDirectory()                 = 0;
	virtual bool                                 SelectGameByTitleIdOrPath(const QString& title_id,
	                                                                       const QString& installed_path) = 0;
};

struct RazorStoreItem {
	QString     entry_id;
	QString     title_id;
	QString     name;
	QString     subtitle;
	QString     category;
	QString     version;
	QString     author;
	QString     license;
	QString     resolutions;
	QString     controls;
	QString     description;
	QString     format; // "zar", "zip", "7z", "tar.gz", "elf", etc.
	uint64_t    size_bytes = 0;
	QString     sha256;
	QString     download_url;
	QStringList part_urls;
	QStringList part_names;
	QString     local_fallback_path;
	QString     icon_url;
	QString     local_icon_path;
	QString     source_name;
	QString     source_kind; // "razor", "prospero", "custom"
	bool        console_only_format = false;
};

struct RazorStoreInstallResult {
	bool    ok = false;
	QString installed_path;
	QString title_id;
	QString error_message;
};

namespace RazorStore {

[[nodiscard]] QString LocateBundledStoreDir();

[[nodiscard]] uint64_t ParseSizeFieldToBytes(const QString& size_text);

[[nodiscard]] bool ParseCatalogJson(const QByteArray& json_bytes, const QString& base_dir,
                                    QString* out_store_name, QVector<RazorStoreItem>* out_items,
                                    QString* out_error = nullptr,
                                    QStringList* out_nested_source_urls = nullptr);

[[nodiscard]] RazorStoreInstallResult InstallPackageToDirectory(
    const QString& source_package_path, const QString& target_games_dir,
    const QString& preferred_title_id = {}, const QString& preferred_name = {},
    const QString& expected_sha256 = {});

} // namespace RazorStore

class RazorStoreDialog: public QDialog {
	Q_OBJECT

signals:
	void LaunchRequested();

public:
	explicit RazorStoreDialog(IRazorStoreLibrary* library, const QString& settings_file,
	                          QWidget* parent = nullptr);
	~RazorStoreDialog() override;

private:
	class Private;
	Private* m_p = nullptr;
};

#endif // RAZOR_STORE_DIALOG_H
