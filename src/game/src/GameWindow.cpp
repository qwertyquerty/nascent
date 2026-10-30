#include "GameWindow.h"

#include "olcPixelGameEngine.h"

#include "GameNascent.h"
#include "SettingsManager.h"
#include "gse/Platform.h"

#include <algorithm>
#include <iostream>

namespace nascent {
    namespace {
        constexpr int32_t MINIMUM_WINDOW_WIDTH = 640;
        constexpr int32_t MINIMUM_WINDOW_HEIGHT = 360;

        class NascentMain : public olc::PixelGameEngine {
            GameNascent game;

            public:
            explicit NascentMain(std::optional<std::filesystem::path> test_play) : game(this, std::move(test_play)) {
                sAppName = "Nascent";
            }

            bool OnUserCreate() override {
                if (SettingsManager::settings.display_fullscreen) {
                    allow_app_switching_in_fullscreen();
                }

                game.init();

                return true;
            }

            bool OnUserUpdate(float elapsed_time) override {
                game.update(elapsed_time);

                if (game.should_quit()) {
                    return false;
                }

                game.draw(this);
                return true;
            }
        };
    }

    void run_game_window(std::optional<std::filesystem::path> test_play) {
        const Settings& settings = SettingsManager::settings;
        int32_t width = std::max<int32_t>(settings.display_window_width, MINIMUM_WINDOW_WIDTH);
        int32_t height = std::max<int32_t>(settings.display_window_height, MINIMUM_WINDOW_HEIGHT);

        if (!settings.display_fullscreen && fit_window_to_screen(width, height)) {
            std::cerr << "Window: shrunk to " << width << "x" << height << " to fit the screen" << std::endl;
        }

        NascentMain ns_main(std::move(test_play));

        if (ns_main.Construct(width, height, 1, 1, settings.display_fullscreen, false, false, true)) {
            ns_main.Start();
        }
    }
}
