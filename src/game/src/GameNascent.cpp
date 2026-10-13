#include "GameNascent.h"

#include "Account.h"
#include "EntityStarfield.h"
#include "InputManager.h"
#include "LocalBests.h"
#include "olcPixelGameEngine.h"
#include "SceneAccount.h"
#include "SceneList.h"
#include "RichPresence.h"
#include "SceneTitle.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "SubmissionQueue.h"
#include "Utf8Path.h"
#include "TextCache.h"
#include "Ui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <ctime>
#include <format>
#include <limits>

namespace nascent {
    GameNascent::GameNascent(olc::PixelGameEngine* window) : Game(window) {

                                                             };

    GameNascent::~GameNascent() {
        SubmissionQueue::stop();
        RichPresence::shutdown();
        release_scenes();
        scene_list.reset();
        sky.reset();
        skin.reset();
        text_cache_clear();
    };

    void GameNascent::init() {
        Account::load();
        SubmissionQueue::start();
        LocalBests::load();
        scene_transition_time = SettingsManager::settings.scene_transition_time;
        set_volumes(SettingsManager::volume_gain(SettingsManager::settings.master_volume), SettingsManager::volume_gain(SettingsManager::settings.music_volume), SettingsManager::volume_gain(SettingsManager::settings.effects_volume));

        olc::Font::init();

        init_input();
        use_blank_canvas();

        start_time = time(nullptr);
        std::srand(start_time);

        Ui::set_screen(window->GetScreenSize());
        skin = std::make_unique<Skin>(this, SettingsManager::settings.skin);
        skin->set_font_scale(Ui::scale());
        olc::vi2d screen = window->GetScreenSize();
        sky = std::make_unique<EntityStarfield>(skin.get(), olc::vd2d{(double)screen.x, (double)screen.y});
        sky->init(nullptr);

        scene_list = std::make_unique<SceneList>(skin.get(), sky.get());

        set_scene(std::make_unique<SceneTitle>(skin.get(), scene_list.get(), sky.get()));

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

        if (scene_list != nullptr && scene_list->initialized && scene != scene_list.get()) {
            scene_list->on_resize();
        }
    }

    void GameNascent::update(float elapsed_time) {
        if (window->GetKey(InputManager::exit_key).bPressed) {
            quit();
            return;
        }

        if (window->GetKey(InputManager::back_key).bPressed && scene != scene_list.get() && dynamic_cast<SceneTitle*>(scene) == nullptr) {
            play_sound(skin->get_random_menu_pluck());
            set_scene(scene_list.get());
        }

        limit_frame_rate(SettingsManager::settings.frame_rate_limit);
        begin_frame(elapsed_time);
        text_cache_begin_frame();
        handle_dropped_files();

        for (SubmissionNotice& notice : SubmissionQueue::take_notices()) {
            show_toast(std::move(notice.message), notice.is_error);
        }

        toast_timer = std::max(0.0f, toast_timer - elapsed_time);

        this->elapsed_time = elapsed_time;
        scene->update(elapsed_time);
        RichPresence::update(elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        draw_scenes(window);

        if (SettingsManager::settings.show_fps) {
            uint16_t fps = elapsed_time > 0 ? (uint16_t)std::min(1 / elapsed_time, 65535.0f) : 0;
            draw_text(skin->display_font_small, std::format("FPS: {}", fps), window->GetScreenSize().x - Ui::px(100), Ui::px(30), olc::WHITE);
        }

        draw_toast(window);
    }

    void GameNascent::show_toast(std::string message, bool is_error, float duration) {
        toast = std::move(message);
        toast_is_error = is_error;
        toast_timer = duration;
    }

    void GameNascent::notify(const std::string& message, bool is_error) {
        show_toast(message, is_error);
    }

    void GameNascent::handle_dropped_files() {
        if (skin_import.valid() && skin_import.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            SkinImportResult result = skin_import.get();

            if (result.ok) {
                show_toast(std::format("Imported the {} skin, choose it in options (F1)", result.name), false);
            }
            else {
                show_toast(std::format("Could not import the skin: {}", result.error), true);
            }
        }

        for (auto it = beatmap_imports.begin(); it != beatmap_imports.end();) {
            if (it->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++it;
                continue;
            }

            BeatmapDownloads::ImportResult result = it->get();
            it = beatmap_imports.erase(it);

            if (result.ok) {
                show_toast(std::format("Imported {}", result.name), false);
            }
            else {
                show_toast(std::format("Could not import {}: {}", result.name, result.error), true);
            }
        }

        for (const std::string& file : get_frame_dropped_files()) {
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
                show_toast("Drop an osu! .osz beatmap or an Etterna noteskin .zip to import it", true);
                continue;
            }

            if (skin_import.valid()) {
                show_toast("Another skin is still importing", true);
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

    void GameNascent::draw_toast(olc::PixelGameEngine* window) {
        if (toast_timer <= 0 || toast.empty()) {
            return;
        }

        olc::Font& font = skin->display_font_small;
        olc::FontRect bounds = text_bounds(font, toast);
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t padding = Ui::px(TOAST_PADDING);
        int32_t toast_top = Ui::px(TOAST_TOP);
        int32_t width = bounds.size.x + padding * 2;
        int32_t height = metrics.size.y + padding * 2;
        int32_t x = (window->GetScreenSize().x - width) / 2;
        float fade = std::min(1.0f, toast_timer / TOAST_FADE_TIME);

        olc::Pixel border = toast_is_error ? olc::Pixel(235, 80, 80, (uint8_t)(255 * fade)) : olc::Pixel(90, 220, 120, (uint8_t)(255 * fade));
        float left = (float)x;
        float top = (float)toast_top;
        float edge = std::max(1.0f, Ui::pxf(TOAST_BORDER));

        window->FillRectDecal({left, top}, {(float)width, (float)height}, olc::Pixel(0, 0, 0, (uint8_t)(220 * fade)));
        window->FillRectDecal({left, top}, {(float)width, edge}, border);
        window->FillRectDecal({left, top + height - edge}, {(float)width, edge}, border);
        window->FillRectDecal({left, top + edge}, {edge, height - edge * 2}, border);
        window->FillRectDecal({left + width - edge, top + edge}, {edge, height - edge * 2}, border);
        draw_text(font, toast, {x + padding, toast_top + padding - metrics.offset.y}, olc::Pixel(255, 255, 255, (uint8_t)(255 * fade)));
    }
}
