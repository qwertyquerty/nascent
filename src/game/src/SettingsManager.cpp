#include "SettingsManager.h"

#include "Platform.h"
#include "SettingsStore.h"
#include "json.h"
#include "Utf8Path.h"

#include <iostream>
#include <memory>

namespace nascent {
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
        Settings,
        field_scroll_speed,
        field_audio_sync_max_error,
        field_audio_visual_offset,
        field_audio_input_offset,
        title_logo_start_delay,
        title_field_count,
        title_h_scroll_rate,
        play_start_delay,
        scene_transition_time,
        list_preview_fade_in_time,
        skin,
        frame_rate_limit,
        master_volume,
        music_volume,
        effects_volume,
        show_fps,
        list_song_directory,
        discord_application_id,
        server_url,
        beatmap_download_urls,
        lane_keymaps
    )

    Settings SettingsManager::settings = Settings();

    static std::map<uint32_t, VersionedJsonFile::Migration> settings_migrations() {
        return {};
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

        if (!error.empty()) {
            std::cerr << "Settings: " << error << std::endl;
        }

        settings = settings_store()->value;
        settings.list_song_directory = path_utf8(resolve_song_directory(settings.list_song_directory));
        return status;
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
