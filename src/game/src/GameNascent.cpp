#include "GameNascent.h"

#include "Account.h"
#include "EntityStarfield.h"
#include "Favorites.h"
#include "InputManager.h"
#include "LocalBests.h"
#include "olcPixelGameEngine.h"
#include "SceneAccount.h"
#include "SceneList.h"
#include "RichPresence.h"
#include "SceneTitle.h"
#include "SettingsManager.h"
#include "gse/Platform.h"
#include "StatusBar.h"
#include "Skin.h"
#include "SubmissionQueue.h"
#include "ToastOverlay.h"
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
#include <format>
#include <iostream>
#include <string_view>
#include <limits>
#include <optional>
#include <utility>

namespace nascent {
    GameNascent::GameNascent(olc::PixelGameEngine* window) : Game(window) {

                                                             };

    GameNascent::~GameNascent() {
        SubmissionQueue::stop();
        RichPresence::shutdown();
        release_scenes();
        scene_list.reset();
        sky.reset();
        logo_skin.reset();
        skin.reset();
        text_cache_clear();
    };

    void GameNascent::init() {
        install_thread_crash_handler();
        std::string_view commit = NASCENT_BUILD_COMMIT;
        std::cerr << std::format("Nascent {} ({}) on {}", NASCENT_VERSION_STRING, commit.empty() ? "local build" : commit.substr(0, 12), platform_description()) << std::endl;
        Account::load();
        SubmissionQueue::start();
        Updater::check();
        LocalBests::load();
        Favorites::load();
        set_transition(std::make_unique<FadeTransition>(SettingsManager::settings.scene_transition_time));
        audio().set_volumes(SettingsManager::volume_gain(SettingsManager::settings.master_volume), SettingsManager::volume_gain(SettingsManager::settings.music_volume), SettingsManager::volume_gain(SettingsManager::settings.effects_volume));

        olc::Font::init();

        input().start();
        bind_actions();
        use_blank_canvas();

        start_time = time(nullptr);
        std::srand(start_time);

        Ui::set_screen(window->GetScreenSize());
        skin = std::make_unique<Skin>(this, SettingsManager::settings.skin);
        skin->set_font_scale(Ui::scale());
        logo_skin = std::make_unique<Skin>(this, "logo");
        logo_skin->set_font_scale(Ui::scale());
        olc::vi2d screen = window->GetScreenSize();
        sky = std::make_unique<EntityStarfield>(skin.get(), olc::vd2d{(double)screen.x, (double)screen.y});
        sky->init();

        scene_list = std::make_unique<SceneList>(skin.get(), logo_skin.get(), sky.get());

        toasts = &overlays().open(std::make_unique<ToastOverlay>(skin.get()));
        scenes().reset(std::make_unique<SceneTitle>(skin.get(), logo_skin.get(), scene_list.get(), sky.get()));

        RichPresence::set_enabled(SettingsManager::settings.discord_rich_presence);
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
        if (input().key_pressed(InputManager::exit_key)) {
            quit();
            return;
        }

        update_screenshot();
        limit_frame_rate(SettingsManager::settings.frame_rate_limit);
        begin_frame(elapsed_time);
        text_cache_begin_frame();
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
        RichPresence::update(elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        clear_color = skin->color(SkinColor::SCREEN_BACKGROUND);
        draw_scenes(window);

        if (SettingsManager::settings.show_fps) {
            uint16_t fps = elapsed_time > 0 ? (uint16_t)std::min(1 / elapsed_time, 65535.0f) : 0;
            olc::FontRect metrics = text_bounds(skin->display_font_small, "Hgjy");
            int32_t baseline = (StatusBar::height() - metrics.size.y) / 2 - metrics.offset.y;
            draw_text(skin->display_font_small, std::format("FPS: {}", fps), window->GetScreenSize().x - Ui::px(FPS_COUNTER_WIDTH), baseline, skin->color(SkinColor::TEXT));
        }

        overlays().draw(window);
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
        return SettingsManager::settings.show_fps ? Ui::px(FPS_COUNTER_WIDTH) : 0;
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

            if (extension != ".zip") {
                show_toast("Drop an osu! .osz beatmap or an Etterna noteskin .zip to import it", true, ToastOverlay::DEFAULT_TIME);
                continue;
            }

            if (skin_import.valid()) {
                show_toast("Another skin is still importing", true, ToastOverlay::DEFAULT_TIME);
                continue;
            }

            show_toast(std::format("Importing {}...", path_utf8(path.filename())), false, std::numeric_limits<float>::infinity());
            skin_import = std::async(std::launch::async, [path] {
                try {
                    return import_etterna_skin(path, Skin::user_skins_directory(), Skin::builtin_skins_directory());
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
