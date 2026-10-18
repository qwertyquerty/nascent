#pragma once

#include "BeatmapDownloads.h"
#include "EtternaSkin.h"
#include "Screenshots.h"
#include "gse/Game.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
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

        void bind_actions();
        void handle_dropped_files();
        void update_screenshot();
        void on_screen_resize() override;
        void on_back() override;
        void show_toast(std::string message, bool is_error, float duration, std::function<void()> action = {});

        public:
        static constexpr int32_t FPS_COUNTER_WIDTH = 100;
        static constexpr float ACCESS_DENIED_TOAST_TIME = 12.0f;

        static int32_t fps_counter_width();

        GameNascent(olc::PixelGameEngine* window);
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
        void notify(const std::string& message, bool is_error = false) override;
    };
}
