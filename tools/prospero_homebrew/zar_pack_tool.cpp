#include "common/archive.h"
#include "common/stringUtils.h"

#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
	if (argc != 3) {
		std::fprintf(stderr, "Usage: razor_zar_packer <input_dir> <output.zar>\n");
		return 1;
	}
	const auto input_dir  = Common::PathFromUtf8(argv[1]);
	const auto output_zar = Common::PathFromUtf8(argv[2]);
	std::error_code ec;
	std::filesystem::create_directories(output_zar.parent_path(), ec);
	std::filesystem::remove(output_zar, ec);

	if (!Common::PackArchive(input_dir, output_zar, [](uint64_t, uint64_t) { return true; })) {
		std::fprintf(stderr, "PackArchive failed for %s -> %s\n", argv[1], argv[2]);
		return 2;
	}
	const auto reader = Common::OpenArchive(output_zar);
	if (!reader) {
		std::fprintf(stderr, "Verification OpenArchive failed for %s\n", argv[2]);
		return 3;
	}
	return 0;
}
