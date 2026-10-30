#include "GameWindow.h"
#include "gse/Platform.h"
#include "SettingsManager.h"
#include "Updater.h"

using namespace nascent;

int main() {
    if (!attach_parent_console()) {
        redirect_output_to_log(user_config_directory(SettingsManager::APP_NAME) / SettingsManager::LOG_FILE);
    }

    install_crash_handler();

    use_executable_directory_for_assets();
    Updater::recover();
    SettingsManager::load();
    run_game_window();
    Updater::install_and_relaunch();
    return 0;
}
