#pragma once

#include "SkinArchive.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nascent::skin_import {
    using ArchiveEntries = std::vector<std::pair<std::string, mz_uint>>;

    struct Frame {
        mz_uint index;
        int32_t columns = 1;
        int32_t rows = 1;
        int32_t res_width = 0;
        int32_t res_height = 0;
    };

    struct Image {
        int32_t width = 0;
        int32_t height = 0;
        std::vector<uint8_t> pixels;
    };

    class Archive {
        public:
        mz_zip_archive zip = {};
        std::string data;
        bool open = false;

        ~Archive();
        std::optional<std::string> read(mz_uint index, size_t limit);
    };

    std::string lowercase(std::string text);
    std::string trim(std::string_view text);
    std::string folder_of(const std::string& name);
    std::string file_of(const std::string& name);
    std::string safe_skin_name(const std::string& text);
    std::optional<Image> load_frame(Archive& archive, const Frame& frame);
    bool write_png(const std::filesystem::path& path, const Image& image);

    SkinImportResult import_etterna_skin(Archive& archive, const ArchiveEntries& entries, const std::string& root, const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory);
    SkinImportResult import_osu_skin(Archive& archive, const ArchiveEntries& entries, const std::string& root, const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory);
}
