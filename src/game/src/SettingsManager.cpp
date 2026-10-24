#include "SettingsManager.h"

#include "gse/Platform.h"
#include "ServerClient.h"
#include "SettingsStore.h"
#include "json.h"
#include "Utf8Path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace nlohmann {
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json& data, const std::optional<T>& value) {
            if (value) {
                data = *value;
            }
            else {
                data = nullptr;
            }
        }

        static void from_json(const json& data, std::optional<T>& value) {
            if (data.is_null()) {
                value.reset();
            }
            else {
                value = data.get<T>();
            }
        }
    };
}

namespace nascent {
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
        HudPositions,
        fps_x,
        fps_y,
        accuracy_x,
        accuracy_y,
        error_x,
        error_y,
        performance_x,
        performance_y,
        judgement_counter_x,
        judgement_counter_y,
        progress_x,
        progress_y
    )

    template <typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
    void to_json(BasicJsonType& nlohmann_json_j, const Settings& nlohmann_json_t) {
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(
            NLOHMANN_JSON_TO,
            field_scroll_speed,
            field_audio_sync_max_error,
            field_audio_visual_offset,
            field_audio_input_offset,
            field_lane_width_percent,
            field_judge_line_percent,
            field_lane_gap,
            field_upscroll,
            field_show_judgement,
            field_judgement_height,
            field_show_combo,
            field_note_gradient,
            field_snap_colors,
            field_show_judgement_line,
            field_show_accuracy,
            field_show_average_error,
            field_show_error_deviation,
            field_show_performance,
            field_show_judgement_counter,
            field_combo_height,
            field_show_timing_bar,
            field_show_timing_average,
            field_timing_bar_height,
            field_timing_bar_length,
            field_timing_bar_width,
            field_mirror,
            field_instant_restart,
            field_lane_cover,
            field_hitsounds,
            field_background_brightness,
            field_note_opacity,
            field_receptor_opacity,
            hud
        ))
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(
            NLOHMANN_JSON_TO,
            title_logo_start_delay,
            title_field_count,
            title_h_scroll_rate,
            title_songs,
            list_sort,
            list_leaderboard_mode,
            play_start_delay,
            scene_transition_time,
            list_preview_fade_in_time,
            skin,
            display_fullscreen,
            display_borderless,
            display_monitor,
            display_window_width,
            display_window_height,
            frame_rate_limit,
            master_volume,
            music_volume,
            effects_volume,
            audio_output_device,
            show_fps,
            account_screen_seen,
            last_run_version,
            list_song_directory,
            list_linked_directories,
            discord_application_id,
            discord_rich_presence,
            server_url,
            beatmap_download_urls,
            lane_keymaps,
            key_bindings
        ))
    }

    template <typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>
    void from_json(const BasicJsonType& nlohmann_json_j, Settings& nlohmann_json_t) {
        const Settings nlohmann_json_default_obj{};
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(
            NLOHMANN_JSON_FROM_WITH_DEFAULT,
            field_scroll_speed,
            field_audio_sync_max_error,
            field_audio_visual_offset,
            field_audio_input_offset,
            field_lane_width_percent,
            field_judge_line_percent,
            field_lane_gap,
            field_upscroll,
            field_show_judgement,
            field_judgement_height,
            field_show_combo,
            field_note_gradient,
            field_snap_colors,
            field_show_judgement_line,
            field_show_accuracy,
            field_show_average_error,
            field_show_error_deviation,
            field_show_performance,
            field_show_judgement_counter,
            field_combo_height,
            field_show_timing_bar,
            field_show_timing_average,
            field_timing_bar_height,
            field_timing_bar_length,
            field_timing_bar_width,
            field_mirror,
            field_instant_restart,
            field_lane_cover,
            field_hitsounds,
            field_background_brightness,
            field_note_opacity,
            field_receptor_opacity,
            hud
        ))
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(
            NLOHMANN_JSON_FROM_WITH_DEFAULT,
            title_logo_start_delay,
            title_field_count,
            title_h_scroll_rate,
            title_songs,
            list_sort,
            list_leaderboard_mode,
            play_start_delay,
            scene_transition_time,
            list_preview_fade_in_time,
            skin,
            display_fullscreen,
            display_borderless,
            display_monitor,
            display_window_width,
            display_window_height,
            frame_rate_limit,
            master_volume,
            music_volume,
            effects_volume,
            audio_output_device,
            show_fps,
            account_screen_seen,
            last_run_version,
            list_song_directory,
            list_linked_directories,
            discord_application_id,
            discord_rich_presence,
            server_url,
            beatmap_download_urls,
            lane_keymaps,
            key_bindings
        ))
    }

    Settings SettingsManager::settings = Settings();

    static std::map<uint32_t, VersionedJsonFile::Migration> settings_migrations() {
        return {
            {4, [](nlohmann::json& data) {
                 if (auto upscroll = data.find("field_upscroll"); upscroll != data.end() && upscroll->is_boolean() && !upscroll->get<bool>()) {
                     *upscroll = nullptr;
                 }
             }},
            {3, [](nlohmann::json& data) {
                 if (auto gap = data.find("field_lane_gap"); gap != data.end() && gap->is_number_integer()) {
                     int value = gap->get<int>();
                     *gap = value < 0 ? nlohmann::json(nullptr) : nlohmann::json(std::min(value, 255));
                 }

                 if (auto opacity = data.find("field_receptor_opacity"); opacity != data.end() && opacity->is_number_integer() && opacity->get<int>() == 100) {
                     *opacity = nullptr;
                 }
             }},
            {2, [](nlohmann::json& data) {
                 if (auto gradient = data.find("field_note_gradient"); gradient != data.end() && gradient->is_boolean() && gradient->get<bool>()) {
                     *gradient = nullptr;
                 }

                 if (auto opacity = data.find("field_note_opacity"); opacity != data.end() && opacity->is_number_integer() && opacity->get<int>() == 100) {
                     *opacity = nullptr;
                 }
             }},
            {1, [](nlohmann::json& data) {
                 static const std::array<std::pair<const char*, int>, 7> skin_defaults = {{
                     {"field_lane_width_percent", 12},
                     {"field_judge_line_percent", 6},
                     {"field_judgement_height", 45},
                     {"field_combo_height", 41},
                     {"field_timing_bar_height", 0},
                     {"field_timing_bar_length", 33},
                     {"field_timing_bar_width", 5},
                 }};

                 for (const auto& [key, value] : skin_defaults) {
                     auto found = data.find(key);

                     if (found != data.end() && found->is_number_integer() && found->get<int>() == value) {
                         *found = nullptr;
                     }
                 }

                 static const std::array<std::pair<const char*, bool>, 11> skin_toggles = {{
                     {"show_fps", true},
                     {"field_show_judgement_line", true},
                     {"field_show_judgement", true},
                     {"field_show_combo", true},
                     {"field_show_accuracy", true},
                     {"field_show_average_error", true},
                     {"field_show_error_deviation", true},
                     {"field_show_performance", false},
                     {"field_show_judgement_counter", false},
                     {"field_show_timing_bar", true},
                     {"field_show_timing_average", true},
                 }};

                 for (const auto& [key, value] : skin_toggles) {
                     auto found = data.find(key);

                     if (found != data.end() && found->is_boolean() && found->get<bool>() == value) {
                         *found = nullptr;
                     }
                 }
             }},
        };
    }

    static std::unique_ptr<SettingsStore<Settings>>& settings_store() {
        static std::unique_ptr<SettingsStore<Settings>> store;
        return store;
    }

    static std::filesystem::path resolve_song_directory(const std::string& configured) {
        if (!configured.empty() && configured != SettingsManager::LEGACY_SONG_DIRECTORY) {
            return utf8_path(configured);
        }

        std::filesystem::path target = user_config_directory(SettingsManager::APP_NAME) / SettingsManager::SONG_DIRECTORY_NAME;
        std::error_code ec;

        if (std::filesystem::is_directory(SettingsManager::LEGACY_SONG_DIRECTORY, ec) && !std::filesystem::exists(target, ec)) {
            std::filesystem::rename(SettingsManager::LEGACY_SONG_DIRECTORY, target, ec);

            if (ec) {
                std::cerr << "Settings: could not move " << SettingsManager::LEGACY_SONG_DIRECTORY << " to " << path_utf8(target) << ": " << ec.message() << std::endl;
                return SettingsManager::LEGACY_SONG_DIRECTORY;
            }
        }

        std::filesystem::create_directories(target, ec);
        return target;
    }

    std::filesystem::path SettingsManager::song_directory() {
        return utf8_path(settings.list_song_directory);
    }

    std::filesystem::path SettingsManager::get_settings_path() {
        return user_config_directory(APP_NAME) / SETTINGS_FILE_NAME;
    }

    SettingsLoadStatus SettingsManager::load() {
        return load(get_settings_path());
    }

    SettingsLoadStatus SettingsManager::load(const std::filesystem::path& path) {
        settings_store() = std::make_unique<SettingsStore<Settings>>(path, SETTINGS_VERSION, settings_migrations());

        std::string error;
        SettingsLoadStatus status = settings_store()->load(error);
        load_status = status;

        if (!error.empty()) {
            std::cerr << "Settings: " << error << std::endl;
        }

        settings = settings_store()->value;
        settings.server_url = server_host(settings.server_url);
        settings.list_song_directory = path_utf8(resolve_song_directory(settings.list_song_directory));
        return status;
    }

    WindowMode SettingsManager::window_mode() {
        if (!settings.display_fullscreen) {
            return WindowMode::WINDOWED;
        }

        return settings.display_borderless ? WindowMode::BORDERLESS : WindowMode::FULLSCREEN;
    }

    bool SettingsManager::apply_window_mode() {
        int32_t width = settings.display_window_width;
        int32_t height = settings.display_window_height;
        return place_window(window_mode(), settings.display_monitor, width, height);
    }

    float SettingsManager::volume_gain(uint8_t percent) {
        if (percent == 0) {
            return 0.0f;
        }

        double fraction = std::min<uint8_t>(percent, 100) / 100.0;
        return (float)std::pow(10.0, (fraction - 1.0) * VOLUME_RANGE_DB / 20.0);
    }

    bool SettingsManager::save() {
        if (settings_store() == nullptr) {
            settings_store() = std::make_unique<SettingsStore<Settings>>(get_settings_path(), SETTINGS_VERSION, settings_migrations());
        }

        settings_store()->value = settings;

        std::string error;

        if (!settings_store()->save(error)) {
            std::cerr << "Settings: " << error << std::endl;
            return false;
        }

        return true;
    }
}
