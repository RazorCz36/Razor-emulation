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

TEST(RazorStoreTests, RejectsMismatchedSha256AndUnwrapsNestedArchives) {
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

	// Build a nested .tar.gz archive (OuterWrapper/InnerGame/eboot.bin) without param.json
	// and verify auto-unwrapping + automatic param.json generation!
	const QString build_tree = QDir(temp.path()).filePath(QStringLiteral("pack_root"));
	const QString inner_game = QDir(build_tree).filePath(QStringLiteral("Outer/MyHomebrew"));
	ASSERT_TRUE(QDir().mkpath(inner_game));
	QFile fake_eboot(QDir(inner_game).filePath(QStringLiteral("eboot.bin")));
	ASSERT_TRUE(fake_eboot.open(QIODevice::WriteOnly));
	fake_eboot.write("ELF_PAYLOAD");
	fake_eboot.close();

	const QString archive_path = QDir(temp.path()).filePath(QStringLiteral("nested_homebrew.tar.gz"));
	QProcess      tar_proc;
	tar_proc.start(QStringLiteral("tar"),
	               {QStringLiteral("-czf"), archive_path, QStringLiteral("-C"), build_tree,
	                QStringLiteral("Outer")});
	ASSERT_TRUE(tar_proc.waitForFinished(15000));
	ASSERT_EQ(tar_proc.exitCode(), 0);

	const auto nested_res = RazorStore::InstallPackageToDirectory(
	    archive_path, target_games, QStringLiteral("PPSA90999"), QStringLiteral("Nested Homebrew"));
	ASSERT_TRUE(nested_res.ok) << nested_res.error_message.toStdString();
	EXPECT_EQ(nested_res.title_id, QStringLiteral("PPSA90999"));
	EXPECT_TRUE(QFileInfo::exists(QDir(nested_res.installed_path).filePath(QStringLiteral("eboot.bin"))));
	EXPECT_TRUE(QFileInfo::exists(
	    QDir(nested_res.installed_path).filePath(QStringLiteral("sce_sys/param.json"))));
}

int main(int argc, char** argv) {
	qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
	QCoreApplication app(argc, argv);
	::testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
