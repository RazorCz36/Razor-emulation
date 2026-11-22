#include "common/archive.h"
#include "gameContent.h"
#include "loader/elf.h"
#include "razorStoreDialog.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {

QString Sha256OfFile(const QString& path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return {};
	}
	QCryptographicHash h(QCryptographicHash::Sha256);
	h.addData(&f);
	return QString::fromLatin1(h.result().toHex()).toLower();
}

} // namespace

TEST(RazorStoreTests, ParsesBundledCatalogAndVerifiesProsperoZars) {
	const QString store_dir =
	    QDir::cleanPath(QString::fromUtf8(RAZOR_SOURCE_ROOT) + QStringLiteral("/store"));
	ASSERT_TRUE(QDir(store_dir).exists()) << "Expected store/ directory at " << store_dir.toStdString();

	QFile cat_file(QDir(store_dir).filePath(QStringLiteral("catalog.json")));
	ASSERT_TRUE(cat_file.open(QIODevice::ReadOnly));

	QString                 store_name;
	QVector<RazorStoreItem> items;
	QString                 err;
	ASSERT_TRUE(
	    RazorStore::ParseCatalogJson(cat_file.readAll(), store_dir, &store_name, &items, &err))
	    << err.toStdString();
	EXPECT_FALSE(store_name.isEmpty());
	ASSERT_GE(items.size(), 3);

	QTemporaryDir temp_games;
	ASSERT_TRUE(temp_games.isValid());

	for (const RazorStoreItem& item: items) {
		ASSERT_FALSE(item.local_fallback_path.isEmpty())
		    << "Missing bundled .zar for " << item.title_id.toStdString();
		ASSERT_TRUE(QFileInfo::exists(item.local_fallback_path));
		EXPECT_EQ(Sha256OfFile(item.local_fallback_path), item.sha256);

		// Install into clean Games directory and verify zero-manual-setup layout:
		const auto res = RazorStore::InstallPackageToDirectory(
		    item.local_fallback_path, temp_games.path(), item.title_id, item.name, item.sha256);
		ASSERT_TRUE(res.ok) << res.error_message.toStdString();
		EXPECT_EQ(res.title_id, item.title_id);
		EXPECT_TRUE(QFileInfo::exists(res.installed_path));
		EXPECT_TRUE(GameContent::FileExists(res.installed_path, QStringLiteral("eboot.bin")));
		EXPECT_TRUE(
		    GameContent::FileExists(res.installed_path, QStringLiteral("sce_sys/param.json")));

		// Extract eboot.bin from the installed .zar and validate Prospero ELF64 header + dynamic sections!
		const QByteArray eboot_bytes =
		    GameContent::ReadFile(res.installed_path, QStringLiteral("eboot.bin"), 8 * 1024 * 1024);
		ASSERT_GT(eboot_bytes.size(), 4096);

		const QString tmp_elf_path =
		    QDir(temp_games.path()).filePath(item.title_id + QStringLiteral("_eboot.bin"));
		QFile tmp_elf(tmp_elf_path);
		ASSERT_TRUE(tmp_elf.open(QIODevice::WriteOnly | QIODevice::Truncate));
		tmp_elf.write(eboot_bytes);
		tmp_elf.close();

		Loader::Elf64 elf;
		elf.Open(GameContent::ToPath(tmp_elf_path));
		EXPECT_TRUE(elf.IsValid()) << "Invalid Prospero ELF for " << item.title_id.toStdString();
		EXPECT_FALSE(elf.IsShared());
		EXPECT_TRUE(elf.IsNextGen());
		EXPECT_NE(elf.GetEntry(), 0u);
		EXPECT_TRUE(elf.HasDynValue(Loader::DT_OS_IMPORT_LIB));
		EXPECT_TRUE(elf.HasDynValue(Loader::DT_OS_NEEDED_MODULE));
		EXPECT_TRUE(elf.HasDynValue(Loader::DT_OS_SYMTABSZ));
	}
}

TEST(RazorStoreTests, RejectsMismatchedSha256HtmlAndUnwrapsNestedArchivesWithContentId) {
	QTemporaryDir temp;
	ASSERT_TRUE(temp.isValid());

	const QString store_dir =
	    QDir::cleanPath(QString::fromUtf8(RAZOR_SOURCE_ROOT) + QStringLiteral("/store"));
	const QString sample_zar = QDir(store_dir).filePath(QStringLiteral("packages/PPSA90001.zar"));
	ASSERT_TRUE(QFileInfo::exists(sample_zar));

	const QString target_games = QDir(temp.path()).filePath(QStringLiteral("Games"));
	const auto    bad_sha_res  = RazorStore::InstallPackageToDirectory(
        sample_zar, target_games, QStringLiteral("PPSA90001"), QStringLiteral("Test"),
        QStringLiteral("0000000000000000000000000000000000000000000000000000000000000000"));
	EXPECT_FALSE(bad_sha_res.ok);

	// Reject HTML CAPTCHA page masquerading as a .zip archive:
	const QString html_zip = QDir(temp.path()).filePath(QStringLiteral("captcha.zip"));
	QFile         html_f(html_zip);
	ASSERT_TRUE(html_f.open(QIODevice::WriteOnly));
	html_f.write("<!DOCTYPE html><html><head><title>Solve CAPTCHA</title></head></html>");
	html_f.close();
	const auto html_res = RazorStore::InstallPackageToDirectory(html_zip, target_games);
	EXPECT_FALSE(html_res.ok);

	// Build a nested .tar.gz archive (Outer/InnerGame/eboot.bin + sce_sys/param.json with contentId)
	const QString build_tree = QDir(temp.path()).filePath(QStringLiteral("pack_root"));
	const QString inner_game = QDir(build_tree).filePath(QStringLiteral("Outer/MyHomebrew"));
	ASSERT_TRUE(QDir().mkpath(QDir(inner_game).filePath(QStringLiteral("sce_sys"))));
	QFile fake_eboot(QDir(inner_game).filePath(QStringLiteral("eboot.bin")));
	ASSERT_TRUE(fake_eboot.open(QIODevice::WriteOnly));
	fake_eboot.write("ELF_PAYLOAD");
	fake_eboot.close();

	QFile param_f(QDir(inner_game).filePath(QStringLiteral("sce_sys/param.json")));
	ASSERT_TRUE(param_f.open(QIODevice::WriteOnly));
	param_f.write(R"({"contentId": "ED5843-PPSA28319_00-XXXXXXXXXXXXXXXX", "contentVersion": "01.000.000"})");
	param_f.close();

	const QString archive_path = QDir(temp.path()).filePath(QStringLiteral("nested_homebrew.tar.gz"));
	QProcess      tar_proc;
	tar_proc.start(QStringLiteral("tar"),
	               {QStringLiteral("-czf"), archive_path, QStringLiteral("-C"), build_tree,
	                QStringLiteral("Outer")});
	ASSERT_TRUE(tar_proc.waitForFinished(15000));
	ASSERT_EQ(tar_proc.exitCode(), 0);

	const auto nested_res =
	    RazorStore::InstallPackageToDirectory(archive_path, target_games, {}, QStringLiteral("Ping Pong"));
	ASSERT_TRUE(nested_res.ok) << nested_res.error_message.toStdString();
	EXPECT_EQ(nested_res.title_id, QStringLiteral("PPSA28319"));
	EXPECT_TRUE(QFileInfo::exists(QDir(nested_res.installed_path).filePath(QStringLiteral("eboot.bin"))));
}

TEST(RazorStoreTests, ParsesPs5LibraryProsperoStoreAndPegasusCatalogSchemas) {
	// 1. Verify human size parser ("52GB", "700 MB", "12456020")
	EXPECT_EQ(RazorStore::ParseSizeFieldToBytes(QStringLiteral("12456020")), 12456020ULL);
	EXPECT_EQ(RazorStore::ParseSizeFieldToBytes(QStringLiteral("700 MB")),
	          700ULL * 1024ULL * 1024ULL);
	EXPECT_EQ(RazorStore::ParseSizeFieldToBytes(QStringLiteral("52GB")),
	          52ULL * 1024ULL * 1024ULL * 1024ULL);

	// 2. Verify deox1111/ps5-library "entries" catalog schema (including multi-part & console image detection)
	const QByteArray ps5_lib_json = R"JSON({
		"name": "My PS5 Library Vault",
		"entries": [
			{
				"id": "game-zip-1",
				"title": "Cyber Arena (v1.04)",
				"title_id": "PPSA01234",
				"kind": "game",
				"platform": "ps5",
				"format": "7z",
				"size": "2 GB",
				"region": "EUR",
				"firmware": "4.xx",
				"cover": "https://example.com/cover.png",
				"files": [
					{"url": "https://mirror.example/PPSA01234.7z.001", "name": "PPSA01234.7z.001"},
					{"url": "https://mirror.example/PPSA01234.7z.002", "name": "PPSA01234.7z.002"}
				]
			},
			{
				"id": "console-only-img",
				"title": "Shadow Image",
				"title_id": "PPSA09999",
				"format": "ffpfsc",
				"files": [{"url": "https://mirror.example/PPSA09999.ffpfsc"}]
			},
			{
				"title": "Skipped PS4 Entry",
				"platform": "ps4",
				"files": [{"url": "https://mirror.example/CUSA00001.pkg"}]
			}
		]
	})JSON";

	QString                 lib_name;
	QVector<RazorStoreItem> lib_items;
	QString                 err;
	ASSERT_TRUE(RazorStore::ParseCatalogJson(ps5_lib_json, {}, &lib_name, &lib_items, &err))
	    << err.toStdString();
	EXPECT_EQ(lib_name, QStringLiteral("My PS5 Library Vault"));
	ASSERT_EQ(lib_items.size(), 2);
	EXPECT_EQ(lib_items[0].name, QStringLiteral("Cyber Arena"));
	EXPECT_EQ(lib_items[0].version, QStringLiteral("1.04"));
	EXPECT_EQ(lib_items[0].title_id, QStringLiteral("PPSA01234"));
	EXPECT_EQ(lib_items[0].part_urls.size(), 2);
	EXPECT_FALSE(lib_items[0].console_only_format);
	EXPECT_TRUE(lib_items[1].console_only_format);

	// 3. Verify ProsperoStore / darkxex/ps5-homebrew-catalog "apps" schema
	const QByteArray prospero_json = R"({
		"schema": 1,
		"name": "PS5 Homebrew Catalog",
		"apps": [
			{
				"titleid": "PPSA28319",
				"name": "Middle Ages Ping Pong",
				"kind": "game",
				"category": "homebrew-games",
				"author": "STL1te",
				"version": "01.000.000",
				"artifact_url": "https://github.com/STL1te/MedievalTennis/releases/download/01.000.000/PPSA28319.zip",
				"sha256": "deadbeef",
				"format": "zip"
			},
			{
				"titleid": "PPSA99039",
				"name": "Console Image App",
				"kind": "app",
				"format": "ffpfsc",
				"artifact_url": "https://example.com/app.ffpfsc"
			}
		]
	})";

	QString                 prospero_name;
	QVector<RazorStoreItem> prospero_items;
	ASSERT_TRUE(
	    RazorStore::ParseCatalogJson(prospero_json, {}, &prospero_name, &prospero_items, &err))
	    << err.toStdString();
	ASSERT_EQ(prospero_items.size(), 1);
	EXPECT_EQ(prospero_items[0].title_id, QStringLiteral("PPSA28319"));
	EXPECT_EQ(prospero_items[0].icon_url,
	          QStringLiteral("https://prosperostore.net/api/v1/icons/PPSA28319-256.png"));

	// 4. Verify Pegasus DL "packages" + "sources" schemas
	const QByteArray pegasus_sources = R"({
		"name": "My Source List",
		"sources": ["https://example.com/cat1.json", {"url": "https://example.com/cat2.json", "enabled": true}]
	})";
	QStringList nested_urls;
	ASSERT_TRUE(RazorStore::ParseCatalogJson(pegasus_sources, {}, &lib_name, &lib_items, &err,
	                                         &nested_urls));
	EXPECT_EQ(nested_urls.size(), 2);
}

int main(int argc, char** argv) {
	qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
	QCoreApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
