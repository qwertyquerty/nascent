#pragma once

#include "SettingsStore.h"
#include "gse/Platform.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    enum class TitleSongs : uint8_t {
        ALL,
        FAVORITES,
        NONE,
    };

    struct HudPositions {
        std::optional<uint8_t> fps_x;
        std::optional<uint8_t> fps_y;
        std::optional<uint8_t> accuracy_x;
        std::optional<uint8_t> accuracy_y;
        std::optional<uint8_t> error_x;
        std::optional<uint8_t> error_y;
        std::optional<uint8_t> performance_x;
        std::optional<uint8_t> performance_y;
        std::optional<uint8_t> judgement_counter_x;
        std::optional<uint8_t> judgement_counter_y;
        std::optional<uint8_t> progress_x;
        std::optional<uint8_t> progress_y;
    };

    struct Settings {
        uint16_t field_scroll_speed = 160; // vh / s
        uint8_t field_audio_sync_max_error = 20; // ms
        int16_t field_audio_visual_offset = 0; //ms
        int16_t field_audio_input_offset = 0; // ms
        std::optional<uint8_t> field_lane_width_percent; // % of the screen height
        std::optional<uint8_t> field_judge_line_percent; // % of the screen height
        int16_t field_lane_gap = -1; // px, -1 uses the skin's spacing
        bool field_upscroll = false;
        std::optional<bool> field_show_judgement;
        std::optional<uint8_t> field_judgement_height; // % of the field above the judgement line's end
        std::optional<bool> field_show_combo;
        bool field_note_gradient = true;
        std::optional<bool> field_show_judgement_line;
        std::optional<bool> field_show_accuracy;
        std::optional<bool> field_show_average_error;
        std::optional<bool> field_show_error_deviation;
        std::optional<bool> field_show_performance;
        std::optional<bool> field_show_judgement_counter;
        std::optional<uint8_t> field_combo_height; // % of the field above the judgement line's end
        std::optional<bool> field_show_timing_bar;
        std::optional<bool> field_show_timing_average;
        std::optional<uint8_t> field_timing_bar_height; // % of the screen above its bottom edge
        std::optional<uint8_t> field_timing_bar_length; // % of the screen width
        std::optional<uint8_t> field_timing_bar_width; // % of the screen height
        bool field_mirror = false;
        bool field_instant_restart = false;
        int8_t field_lane_cover = 0;
        bool field_hitsounds = false;
        uint8_t field_background_brightness = 0;
        uint8_t field_note_opacity = 100;
        uint8_t field_receptor_opacity = 100;
        HudPositions hud;

        float title_logo_start_delay = 1; // s
        uint8_t title_field_count = 9;
        uint16_t title_h_scroll_rate = 250; // px / s
        TitleSongs title_songs = TitleSongs::ALL;
        uint8_t list_sort = 0;
        uint8_t list_leaderboard_mode = 0;

        float play_start_delay = 2; // s

        float scene_transition_time = 0.2f; // s
        float list_preview_fade_in_time = 0.5; // s

        std::string skin = "arrows_filled";
        bool display_fullscreen = true;
        bool display_borderless = false;
        std::string display_monitor;
        uint16_t display_window_width = 1600;
        uint16_t display_window_height = 900;
        uint16_t frame_rate_limit = 1000;
        uint8_t master_volume = 50;
        uint8_t music_volume = 75;
        uint8_t effects_volume = 75;
        std::string audio_output_device;
        std::optional<bool> show_fps;
        bool account_screen_seen = false;
        std::string last_run_version;

        std::string list_song_directory;
        std::vector<std::string> list_linked_directories;

        std::string discord_application_id = "1360933253070061718";
        bool discord_rich_presence = true;

        std::string server_url = "dev-ns.madeline.sh";
        std::vector<std::string> beatmap_download_urls = {"https://catboy.best/d/{id}", "https://osu.direct/api/d/{id}"};

        std::map<std::string, std::vector<std::string>> lane_keymaps = {
            {"4", {"Z", "X", "PERIOD", "OEM_2"}},
            {"7", {"Z", "X", "C", "SPACE", "COMMA", "PERIOD", "OEM_2"}}
        };

        std::map<std::string, std::string> key_bindings;
    };

    class SettingsManager {
        public:
        static constexpr const char* APP_NAME = "Nascent";
        static constexpr const char* LOG_FILE = "nascent.log";
        static constexpr const char* SETTINGS_FILE_NAME = "settings.json";
        static constexpr double VOLUME_RANGE_DB = 40.0;
        static constexpr uint32_t SETTINGS_VERSION = 2;
        static constexpr const char* SONG_DIRECTORY_NAME = "songs";
        static constexpr const char* LEGACY_SONG_DIRECTORY = "assets/songs";

        static Settings settings;
        static inline SettingsLoadStatus load_status = SettingsLoadStatus::CREATED;

        static std::filesystem::path get_settings_path();
        static std::filesystem::path song_directory();
        static SettingsLoadStatus load();
        static SettingsLoadStatus load(const std::filesystem::path& path);
        static bool save();
        static float volume_gain(uint8_t percent);
        static WindowMode window_mode();
        static bool apply_window_mode();
    };
}
