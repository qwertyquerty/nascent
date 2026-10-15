#pragma once

#include "BeatmapDownloads.h"
#include "EtternaSkin.h"
#include "gse/Game.h"

#include <cstdint>
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
        std::unique_ptr<EntityStarfield> sky;
        std::unique_ptr<SceneList> scene_list;
        float elapsed_time = 0;
        int64_t start_time;

        std::future<SkinImportResult> skin_import;
        std::vector<std::future<BeatmapDownloads::ImportResult>> beatmap_imports;
        ToastOverlay* toasts = nullptr;

        void bind_actions();
        void handle_dropped_files();
        void on_screen_resize() override;
        void on_back() override;
        void show_toast(std::string message, bool is_error, float duration);

        public:

        GameNascent(olc::PixelGameEngine* window);
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
        void notify(const std::string& message, bool is_error = false) override;
    };
}
