#include "olcPixelGameEngine.h"

#include "EditorGame.h"
#include "SettingsManager.h"
#include "Updater.h"
#include "gse/Platform.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>

using namespace nascent;

static constexpr int32_t DEFAULT_WINDOW_WIDTH = 1600;
static constexpr int32_t DEFAULT_WINDOW_HEIGHT = 900;
static constexpr const char* LOG_FILE = "editor.log";

class EditorMain : public olc::PixelGameEngine {
    EditorGame editor;
    uint32_t shown_fps = 0;

    public:
    EditorMain() : editor(this) {
        sAppName = "Nascent Editor";
    }

    bool OnUserCreate() override {
        CreateLayer();
        editor.init();
        SetLayerCustomRenderFunction(0, [this] { editor.render_interface(); });
        return true;
    }

    bool OnUserUpdate(float elapsed_time) override {
        editor.update(elapsed_time);

        if (editor.should_quit()) {
            return false;
        }

        editor.draw(this);

        if (uint32_t fps = GetFPS(); fps != shown_fps) {
            shown_fps = fps;
            set_window_title(std::format("{} - {} fps", sAppName, fps));
        }

        return true;
    }

    bool OnUserDestroy() override {
        return editor.can_close();
    }
};

static void find_assets() {
    use_executable_directory_for_assets();
    std::error_code ec;

    if (std::filesystem::is_directory("assets", ec)) {
        return;
    }

    for (std::filesystem::path folder = executable_directory(); !folder.empty() && folder.has_parent_path() && folder.parent_path() != folder; folder = folder.parent_path()) {
        if (std::filesystem::is_directory(folder / "assets" / "skins", ec)) {
            std::filesystem::current_path(folder, ec);
            return;
        }
    }
}

int main() {
    if (!attach_parent_console()) {
        redirect_output_to_log(user_config_directory(SettingsManager::APP_NAME) / LOG_FILE);
    }

    install_crash_handler();
    find_assets();
    Updater::recover();
    SettingsManager::load();

    int32_t width = DEFAULT_WINDOW_WIDTH;
    int32_t height = DEFAULT_WINDOW_HEIGHT;
    fit_window_to_screen(width, height);

    {
        EditorMain app;

        if (app.Construct(width, height, 1, 1, false, false, false, true)) {
            app.Start();
        }
    }

    Updater::install_and_relaunch();
    return 0;
}
