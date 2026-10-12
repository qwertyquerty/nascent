#pragma once

#include "EtternaSkin.h"
#include "Game.h"

#include <cstdint>
#include <future>
#include <memory>
#include <string>

namespace nascent {
    class SceneList;
    class Skin;

    class GameNascent : public Game {
        protected:
        std::unique_ptr<Skin> skin;
        std::unique_ptr<SceneList> scene_list;
        float elapsed_time = 0;
        int64_t start_time;

        std::future<SkinImportResult> skin_import;
        std::string toast;
        float toast_timer = 0;
        bool toast_is_error = false;

        void handle_dropped_files();
        void show_toast(std::string message, bool is_error, float duration = TOAST_TIME);
        void draw_toast(olc::PixelGameEngine* window);

        public:
        static constexpr float TOAST_TIME = 5.0f;
        static constexpr float TOAST_FADE_TIME = 0.5f;
        static constexpr int32_t TOAST_PADDING = 16;
        static constexpr int32_t TOAST_TOP = 60;
        static constexpr int32_t TOAST_BORDER = 2;

        GameNascent(olc::PixelGameEngine* window);
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;

        void discord_init();
        void discord_update();
    };
}
