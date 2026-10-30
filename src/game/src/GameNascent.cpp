#include "GameNascent.h"

#include "Account.h"
#include "ChangelogOverlay.h"
#include "Chart.h"
#include "ChartFolders.h"
#include "ChartOffsets.h"
#include "EntityStarfield.h"
#include "Favorites.h"
#include "InputManager.h"
#include "LocalBests.h"
#include "olcPixelGameEngine.h"
#include "SceneAccount.h"
#include "SceneList.h"
#include "ScenePlay.h"
#include "RichPresence.h"
#include "SceneTitle.h"
#include "SettingsManager.h"
#include "gse/Platform.h"
#include "StatusBar.h"
#include "Skin.h"
#include "SkinSettings.h"
#include "SubmissionQueue.h"
#include "ToastOverlay.h"
#include "ReleaseUpdate.h"
#include "Updater.h"
#include "Utf8Path.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "gse/render/Color.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace nascent {
    namespace {
        constexpr float FPS_REFRESH_SECONDS = 0.25f;

        struct FpsReadout {
            float window = 0;
            uint32_t frames = 0;
            std::optional<uint16_t> shown;
        };

        FpsReadout fps_readout;
    }

    GameNascent::GameNascent(olc::PixelGameEngine* window, std::optional<std::filesystem::path> test_play) : Game(window), test_play(std::move(test_play)) {

                                                                                                             };

    GameNascent::~GameNascent() {
        SubmissionQueue::stop();
        LocalBests::flush();
        RichPresence::shutdown();
        release_scenes();
        scene_list.reset();
        sky.reset();
        Ui::set_layout_theme(nullptr);
        logo_skin.reset();
        skin.reset();
        text_cache_clear();
    };

    std::filesystem::path GameNascent::test_play_file() {
        return std::filesystem::temp_directory_path() / "nascent_editor" / "test_play.json";
    }

    void GameNascent::init() {
        install_thread_crash_handler();
        std::string_view commit = NASCENT_BUILD_COMMIT;
        std::cerr << std::format("Nascent {} ({}) on {}{}", NASCENT_VERSION_STRING, commit.empty() ? "local build" : commit.substr(0, 12), platform_description(), test_play ? ", test play from the editor" : "") << std::endl;
        Account::load();
        std::error_code skins_ec;
        std::filesystem::create_directories(Skin::user_skins_directory(), skins_ec);

        if (!test_play) {
            SubmissionQueue::start();
            Updater::check();
            server_info = ServerApi::info();
            pending_changelog = Changelog::since_last_run();
        }

        LocalBests::load();
        Favorites::load();
        ChartOffsets::load();
        set_transition(std::make_unique<FadeTransition>(SettingsManager::settings.scene_transition_time));

        if (const std::string& device = SettingsManager::settings.audio_output_device; !device.empty() && !audio().engine().SetPlaybackDevice(device)) {
            std::cerr << "Audio: output device \"" << device << "\" isn't available, using the system default" << std::endl;
        }

        apply_volumes();

        olc::Font::init();

        input().start();

        if (SettingsManager::window_mode() != WindowMode::WINDOWED && window_placement_supported()) {
            SettingsManager::apply_window_mode();
        }
        bind_actions();
        use_blank_canvas();

        start_time = time(nullptr);
        std::srand(start_time);

        Ui::set_screen(window->GetScreenSize());
        skin = std::make_unique<Skin>(this, SettingsManager::settings.skin);
        skin->set_font_scale(Ui::scale());
        Ui::set_layout_theme(skin.get());
        logo_skin = std::make_unique<Skin>(this, "logo");
        logo_skin->set_font_scale(Ui::scale());
        skin->set_logo_skin(logo_skin.get());
        olc::vi2d screen = window->GetScreenSize();
        sky = std::make_unique<EntityStarfield>(skin.get(), olc::vd2d{(double)screen.x, (double)screen.y});
        sky->init();

        scene_list = std::make_unique<SceneList>(skin.get(), logo_skin.get(), sky.get());

        toasts = &overlays().open(std::make_unique<ToastOverlay>(skin.get()));

        if (test_play) {
            if (!start_test_play()) {
                quit();
            }

            return;
        }

        scenes().reset(std::make_unique<SceneTitle>(skin.get(), logo_skin.get(), scene_list.get(), sky.get()));

        RichPresence::set_enabled(SettingsManager::settings.discord_rich_presence);
    }

    bool GameNascent::start_test_play() {
        std::ifstream in(*test_play, std::ios::binary);
        nlohmann::json request = in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();

        if (!request.is_object() || !request.contains("chart")) {
            std::cerr << "Test play: couldn't read " << path_utf8(*test_play) << std::endl;
            return false;
        }

        std::string error;
        float rate = std::clamp(request.value("rate", 1.0f), 0.25f, 4.0f);
        test_chart = Chart::load_text(request["chart"].dump(), *test_play, rate, error);

        if (test_chart == nullptr) {
            std::cerr << "Test play: the chart can't be played: " << error << std::endl;
            return false;
        }

        auto play = std::make_unique<ScenePlay>(test_chart.get(), skin.get());
        play->set_test_start(request.value("start", 0.0));
        scenes().reset(std::move(play));
        return true;
    }

    void GameNascent::on_screen_resize() {
        Ui::set_screen(window->GetScreenSize());

        if (skin != nullptr) {
            skin->set_font_scale(Ui::scale());
        }

        if (sky != nullptr) {
            olc::vi2d screen = window->GetScreenSize();
            sky->size = {(double)screen.x, (double)screen.y};
        }

        if (scene_list != nullptr && scene_list->is_created() && !scenes().contains(scene_list.get()) && scenes().outgoing_scene() != scene_list.get()) {
            scene_list->on_resize();
        }
    }

    void GameNascent::bind_actions() {
        input().bind(actions::BACK, {InputManager::back_key});
        input().bind(actions::CONFIRM, {InputManager::menu_start_key});
        input().bind(actions::UP, {InputManager::menu_up_key});
        input().bind(actions::DOWN, {InputManager::menu_down_key});
        input().bind(actions::LEFT, {InputManager::menu_left_key});
        input().bind(actions::RIGHT, {InputManager::menu_right_key});
        input().bind(actions::NEXT_FIELD, {InputManager::field_key});
        input().bind(game_actions::CHART_MENU, {InputManager::chart_menu_key});
        input().bind(game_actions::SORT_MENU, {InputManager::sort_key});
    }

    void GameNascent::on_back() {
        audio().play_effect(skin->get_random_menu_pluck());
    }

    void GameNascent::update(float elapsed_time) {
        if (InputManager::exit_pressed(input())) {
            quit();
            return;
        }

        update_screenshot();
        check_client_version();
        show_pending_changelog();
        limit_frame_rate(SettingsManager::settings.frame_rate_limit);
        begin_frame(elapsed_time);
        text_cache_begin_frame();
        update_volume_keys(elapsed_time);
        handle_dropped_files();

        for (SubmissionNotice& notice : SubmissionQueue::take_notices()) {
            show_toast(std::move(notice.message), notice.is_error, ToastOverlay::DEFAULT_TIME);
        }

        if (std::optional<UpdateNotice> notice = Updater::take_notice()) {
            show_toast(std::move(notice->message), notice->is_error, ToastOverlay::DEFAULT_TIME);
        }

        if (BeatmapDownloads::take_access_denied()) {
            show_toast("Couldn't save songs: allow Nascent in your antivirus or run it as administrator", true, ACCESS_DENIED_TOAST_TIME);
        }

        this->elapsed_time = elapsed_time;
        update_scene(elapsed_time);

        if (test_play && (scenes().depth() == 0 || input().take(actions::BACK))) {
            quit();
            return;
        }

        RichPresence::update(elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        clear_color = skin->color(SkinColor::SCREEN_BACKGROUND);
        draw_scenes(window);

        if (skin_toggle(*skin, skin_toggles::SHOW_FPS)) {
            fps_readout.window += elapsed_time;
            fps_readout.frames++;

            if (!fps_readout.shown || fps_readout.window >= FPS_REFRESH_SECONDS) {
                fps_readout.shown = fps_readout.window > 0 ? (uint16_t)std::min(fps_readout.frames / fps_readout.window, 65535.0f) : 0;
                fps_readout.window = 0;
                fps_readout.frames = 0;
            }

            olc::FontRect bounds = text_bounds(skin->display_font_small, "FPS: 0000");
            olc::vf2d spot = hud_position(*skin, HudElement::FPS, {(float)bounds.size.x, (float)bounds.size.y}, window->GetScreenSize());
            draw_text(skin->display_font_small, std::format("FPS: {}", *fps_readout.shown), (int32_t)spot.x - bounds.offset.x, (int32_t)spot.y - bounds.offset.y, skin->color(SkinColor::TEXT));
        }
        else {
            fps_readout = FpsReadout();
        }

        overlays().draw(window);
    }

    void GameNascent::apply_volumes() {
        const Settings& settings = SettingsManager::settings;
        audio().set_volumes(SettingsManager::volume_gain(settings.master_volume), SettingsManager::volume_gain(settings.music_volume), SettingsManager::volume_gain(settings.effects_volume));
    }

    void GameNascent::change_volume(int32_t delta) {
        Settings& settings = SettingsManager::settings;
        settings.master_volume = (uint8_t)std::clamp((int32_t)settings.master_volume + delta, 0, 100);
        apply_volumes();
        show_toast(std::format("Volume {}%", settings.master_volume), false, VOLUME_TOAST_TIME);
        volume_save_delay = VOLUME_SAVE_DELAY;
    }

    void GameNascent::update_volume_keys(float elapsed_time) {
        if (volume_save_delay > 0) {
            volume_save_delay -= elapsed_time;

            if (volume_save_delay <= 0) {
                SettingsManager::save();
            }
        }

        Input& keys = input();
        int32_t wheel = keys.mouse_wheel();
        bool up = keys.pressed(actions::UP);
        bool down = keys.pressed(actions::DOWN);
        bool up_held = keys.held(actions::UP);
        bool down_held = keys.held(actions::DOWN);

        if ((wheel == 0 && !up_held && !down_held) || !keys.focused() || !alt_held()) {
            return;
        }

        keys.block(actions::UP);
        keys.block(actions::DOWN);
        int32_t steps = (wheel > 0) - (wheel < 0);

        if (wheel != 0) {
            keys.take_wheel();
        }

        if (volume_up_repeat.update(up, up_held, elapsed_time)) {
            steps++;
        }

        if (volume_down_repeat.update(down, down_held, elapsed_time)) {
            steps--;
        }

        if (steps != 0) {
            change_volume(steps * VOLUME_STEP);
        }
    }

    void GameNascent::show_pending_changelog() {
        Scene* scene = scenes().top();

        if (pending_changelog.empty() || scene == nullptr || !scene->is_created() || scenes().is_transitioning()) {
            return;
        }

        std::string subtitle = std::format("You're now on Nascent {}", NASCENT_VERSION_STRING);
        scene->overlays.open(std::make_unique<ChangelogOverlay>(skin.get(), std::exchange(pending_changelog, {}), "What's new", std::move(subtitle)));
    }

    void GameNascent::check_client_version() {
        if (!is_ready(server_info)) {
            return;
        }

        ApiResult<ServerInfo> result = server_info.get();

        if (!result.ok() || result.value->min_client_version.empty()) {
            return;
        }

        std::optional<ReleaseVersion> current = parse_release_version(NASCENT_VERSION_STRING);
        std::optional<ReleaseVersion> minimum = parse_release_version(result.value->min_client_version);

        if (current && minimum && *current < *minimum) {
            std::string message = std::format("This version is too old to submit plays. You can keep playing, but update to {} to submit them", result.value->min_client_version);
            show_toast(std::move(message), true, OUTDATED_CLIENT_TOAST_TIME, [] {
                Updater::start();
            });
        }
    }

    void GameNascent::update_screenshot() {
        if (is_ready(screenshot_save)) {
            ScreenshotResult result = screenshot_save.get();

            if (result.ok) {
                show_toast(std::format("Screenshot saved: {} (click to open)", path_utf8(result.path.filename())), false, ToastOverlay::DEFAULT_TIME, [path = result.path] {
                    open_file(path);
                });
            }
            else {
                show_toast(std::format("Screenshot failed: {}", result.error), true, ToastOverlay::DEFAULT_TIME);
            }
        }

        if (std::exchange(screenshot_requested, false) && !screenshot_save.valid()) {
            if (std::optional<CapturedFrame> frame = capture_displayed_frame()) {
                screenshot_save = std::async(std::launch::async, [frame = std::move(*frame)] {
                    return Screenshots::save(frame);
                });
            }
            else {
                show_toast("Screenshot failed: couldn't read the screen", true, ToastOverlay::DEFAULT_TIME);
            }
        }

        if (input().key_pressed(InputManager::get_key(KeyBinding::SCREENSHOT))) {
            screenshot_requested = true;
        }
    }

    int32_t GameNascent::fps_counter_width() {
        return skin_toggle(skin_toggles::SHOW_FPS) ? Ui::layout_px("fps_counter.width") : 0;
    }

    void GameNascent::show_toast(std::string message, bool is_error, float duration, std::function<void()> action) {
        if (toasts != nullptr) {
            toasts->show(std::move(message), is_error, duration, std::move(action));
        }
    }

    void GameNascent::notify(const std::string& message, bool is_error) {
        show_toast(message, is_error, ToastOverlay::DEFAULT_TIME);
    }

    void GameNascent::handle_dropped_files() {
        if (is_ready(skin_import)) {
            SkinImportResult result = skin_import.get();

            if (result.ok) {
                Skin::list_changed();
                show_toast(std::format("Imported the {} skin, choose it in options (F1)", result.name), false, ToastOverlay::DEFAULT_TIME);
            }
            else {
                show_toast(std::format("Could not import the skin: {}", result.error), true, ToastOverlay::DEFAULT_TIME);
            }
        }

        for (auto it = beatmap_imports.begin(); it != beatmap_imports.end();) {
            if (!is_ready(*it)) {
                ++it;
                continue;
            }

            BeatmapDownloads::ImportResult result = it->get();
            it = beatmap_imports.erase(it);

            if (result.ok) {
                show_toast(std::format("Imported {}", result.name), false, ToastOverlay::DEFAULT_TIME);
            }
            else {
                show_toast(std::format("Could not import {}: {}", result.name, result.error), true, ToastOverlay::DEFAULT_TIME);
            }
        }

        for (const std::string& file : input().dropped_files()) {
            std::filesystem::path path = utf8_path(file);
            std::error_code ec;

            if (std::filesystem::is_directory(path, ec)) {
                FolderLinkResult linked = ChartFolders::link(path);
                show_toast(std::move(linked.message), !linked.ok, ToastOverlay::DEFAULT_TIME);
                continue;
            }

            std::string extension = path_utf8(path.extension());

            for (char& c : extension) {
                c = (char)std::tolower((unsigned char)c);
            }

            if (extension == ".osz") {
                show_toast(std::format("Importing {}...", path_utf8(path.filename())), false, std::numeric_limits<float>::infinity());
                beatmap_imports.push_back(std::async(std::launch::async, [path] {
                    try {
                        return BeatmapDownloads::import_archive(path);
                    }
                    catch (const std::exception& e) {
                        BeatmapDownloads::ImportResult result;
                        result.name = path_utf8(path.stem());
                        result.error = e.what();
                        return result;
                    }
                }));
                continue;
            }

            if (extension != ".zip" && extension != ".osk") {
                show_toast("Drop an osu! .osz beatmap or .osk skin, a song pack .zip, or a Nascent or Etterna skin .zip to import it", true, ToastOverlay::DEFAULT_TIME);
                continue;
            }

            if (extension == ".zip" && BeatmapDownloads::archive_has_charts(path)) {
                show_toast(std::format("Importing {}...", path_utf8(path.filename())), false, std::numeric_limits<float>::infinity());
                beatmap_imports.push_back(std::async(std::launch::async, [path] {
                    try {
                        return BeatmapDownloads::import_pack(path);
                    }
                    catch (const std::exception& e) {
                        BeatmapDownloads::ImportResult result;
                        result.name = path_utf8(path.stem());
                        result.error = e.what();
                        return result;
                    }
                }));
                continue;
            }

            if (skin_import.valid()) {
                show_toast("Another skin is still importing", true, ToastOverlay::DEFAULT_TIME);
                continue;
            }

            show_toast(std::format("Importing {}...", path_utf8(path.filename())), false, std::numeric_limits<float>::infinity());
            skin_import = std::async(std::launch::async, [path] {
                try {
                    return import_skin_archive(path, Skin::user_skins_directory(), Skin::builtin_skins_directory());
                }
                catch (const std::exception& e) {
                    SkinImportResult result;
                    result.error = e.what();
                    return result;
                }
            });
        }
    }
}
