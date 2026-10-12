#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace nascent {
    struct SkinImportResult {
        bool ok = false;
        std::string name;
        std::string error;
    };

    inline constexpr size_t MAX_SKIN_ARCHIVE_BYTES = 64u * 1024u * 1024u;
    inline constexpr size_t MAX_SKIN_IMAGE_BYTES = 16u * 1024u * 1024u;
    inline constexpr size_t MAX_SKIN_NAME_BYTES = 64;

    SkinImportResult import_etterna_skin(const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory);
}
