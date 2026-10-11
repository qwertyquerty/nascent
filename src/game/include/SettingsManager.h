#pragma once

#include "SettingsStore.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nascent {
    struct Settings {
        uint16_t field_scroll_speed = 210; // vh / s
        uint8_t field_audio_sync_max_error = 20; // ms
        int16_t field_audio_visual_offset = 0; //ms
        int16_t field_audio_input_offset = 10; // ms

        float title_logo_start_delay = 1; // s
        uint8_t title_field_count = 9;
        uint16_t title_h_scroll_rate = 250; // px / s

        float play_start_delay = 2; // s

        float scene_transition_time = 0.2f; // s
        float list_preview_fade_in_time = 0.5; // s

        std::string list_song_directory = "assets/songs";

        std::string discord_application_id = "1360933253070061718";

        std::string server_url = "https://dev-ns.madeline.sh";
        std::vector<std::string> beatmap_download_urls = {"https://catboy.best/d/{id}", "https://osu.direct/api/d/{id}"};

        std::map<std::string, std::vector<std::string>> lane_keymaps = {
            {"4", {"Z", "X", "PERIOD", "OEM_2"}},
            {"7", {"Z", "X", "C", "SPACE", "COMMA", "PERIOD", "OEM_2"}}
        };
    };

    class SettingsManager {
        public:
        static constexpr const char* APP_NAME = "Nascent";
        static constexpr const char* SETTINGS_FILE_NAME = "settings.json";
        static constexpr uint32_t SETTINGS_VERSION = 1;

        static Settings settings;

        static std::filesystem::path get_settings_path();
        static SettingsLoadStatus load();
        static SettingsLoadStatus load(const std::filesystem::path& path);
        static bool save();
    };
}
