#include "olcPixelGameEngine.h"

#include "GameNascent.h"
#include "Platform.h"
#include "SettingsManager.h"

#include <algorithm>

using namespace nascent;

static constexpr int32_t MINIMUM_WINDOW_WIDTH = 640;
static constexpr int32_t MINIMUM_WINDOW_HEIGHT = 360;

class NascentMain : public olc::PixelGameEngine {
    GameNascent game;

    public:
    NascentMain() : game(this) {
        sAppName = "Nascent";
    }

    public:
    bool OnUserCreate() override {

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

int main() {
    attach_parent_console();
    use_executable_directory_for_assets();
    SettingsManager::load();

    const Settings& settings = SettingsManager::settings;
    int32_t width = std::max<int32_t>(settings.display_window_width, MINIMUM_WINDOW_WIDTH);
    int32_t height = std::max<int32_t>(settings.display_window_height, MINIMUM_WINDOW_HEIGHT);

    NascentMain ns_main;

    if (ns_main.Construct(width, height, 1, 1, settings.display_fullscreen, false, false, true)) {
        ns_main.Start();
    }

    return 0;
}
