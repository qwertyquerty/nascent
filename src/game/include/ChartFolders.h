#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    struct FolderLinkResult {
        bool ok = false;
        std::string message;
    };

    class ChartFolders {
        public:
        static constexpr size_t SCAN_FOLDER_LIMIT = 200;
        static constexpr const char* OSU_FOLDER_NAME = "osu!";
        static constexpr const char* OSU_DEFAULT_SONGS = "Songs";
        static constexpr const char* OSU_BEATMAP_SETTING = "BeatmapDirectory";

        static std::vector<std::filesystem::path> all();
        static FolderLinkResult link(const std::filesystem::path& folder);
        static FolderLinkResult unlink(size_t index);
        static bool is_linked(const std::filesystem::path& folder);
        static bool in_linked_folder(const std::filesystem::path& file);
        static std::optional<std::filesystem::path> osu_songs_folder();
        static std::string display_name(const std::string& folder);
        static bool take_changed();
    };
}
