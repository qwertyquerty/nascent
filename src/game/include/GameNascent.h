#pragma once

#include "BeatmapDownloads.h"
#include "Changelog.h"
#include "SkinArchive.h"
#include "Screenshots.h"
#include "ServerApi.h"
#include "gse/Game.h"
#include "gse/ui/KeyRepeat.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Chart;
    class EntityStarfield;
    class SceneList;
    class Skin;
    class ToastOverlay;

    class GameNascent : public Game {
        protected:
        std::unique_ptr<Skin> skin;
        std::unique_ptr<Skin> logo_skin;
        std::unique_ptr<EntityStarfield> sky;
        std::unique_ptr<SceneList> scene_list;
        float elapsed_time = 0;
        int64_t start_time;

        std::future<SkinImportResult> skin_import;
        std::vector<std::future<BeatmapDownloads::ImportResult>> beatmap_imports;
        ToastOverlay* toasts = nullptr;
        bool screenshot_requested = false;
        std::future<ScreenshotResult> screenshot_save;
        std::future<ApiResult<ServerInfo>> server_info;
        std::vector<ChangelogRelease> pending_changelog;
        std::optional<std::filesystem::path> test_play;
        std::unique_ptr<Chart> test_chart;
        KeyRepeat volume_up_repeat;
        KeyRepeat volume_down_repeat;
        float volume_save_delay = 0;

        void bind_actions();
        void handle_dropped_files();
        void update_screenshot();
        void check_client_version();
        void show_pending_changelog();
        void update_volume_keys(float elapsed_time);
        void change_volume(int32_t delta);
        void apply_volumes();
        void on_screen_resize() override;
        void on_back() override;
        void show_toast(std::string message, bool is_error, float duration, std::function<void()> action = {});
        bool start_test_play();

        public:
        static constexpr float ACCESS_DENIED_TOAST_TIME = 12.0f;
        static constexpr float OUTDATED_CLIENT_TOAST_TIME = 15.0f;
        static constexpr int32_t VOLUME_STEP = 5;
        static constexpr float VOLUME_TOAST_TIME = 1.5f;
        static constexpr float VOLUME_SAVE_DELAY = 1.0f;

        static constexpr std::string_view TEST_PLAY_ARGUMENT = "--test-play";

        static int32_t fps_counter_width();
        static std::filesystem::path test_play_file();

        GameNascent(olc::PixelGameEngine* window, std::optional<std::filesystem::path> test_play = std::nullopt);
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
        void notify(const std::string& message, bool is_error = false) override;
    };
}
