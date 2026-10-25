#pragma once

#include "OsuExport.h"

#include <filesystem>
#include <string>

namespace nascent {
    struct ChartData;

    enum class ArchiveFormat {
        OSZ,
        NASCENT,
    };

    bool export_archive(const ChartData& data, const std::filesystem::path& folder, const std::filesystem::path& target, ArchiveFormat format, const OsuDifficulty& difficulty, std::string& error);
}
