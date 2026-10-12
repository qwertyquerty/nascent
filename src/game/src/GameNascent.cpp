#include "GameNascent.h"

#include "Account.h"
#include "InputManager.h"
#include "LocalBests.h"
#include "olcPixelGameEngine.h"
#include "SceneAccount.h"
#include "SceneList.h"
#include "SceneTitle.h"
#include "discord-rpc.hpp"
#include "SettingsManager.h"
#include "Skin.h"
#include "Utf8Path.h"
#include "TextCache.h"

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
        discord::RPCManager::get().shutdown();
        release_scenes();
        scene_list.reset();
        skin.reset();
        text_cache_clear();
    };

    void GameNascent::init() {
        SettingsManager::load();
        Account::load();
        LocalBests::load();
        scene_transition_time = SettingsManager::settings.scene_transition_time;
        set_volumes(SettingsManager::settings.master_volume / 100.0f, SettingsManager::settings.music_volume / 100.0f, SettingsManager::settings.effects_volume / 100.0f);

        olc::Font::init();

        init_input();
        use_blank_canvas();

        start_time = time(nullptr);
        std::srand(start_time);

        skin = std::make_unique<Skin>(this, SettingsManager::settings.skin);
        scene_list = std::make_unique<SceneList>(skin.get());

        set_scene(std::make_unique<SceneTitle>(skin.get(), scene_list.get()));

        discord_init();
        discord_update();
    }

    void GameNascent::update(float elapsed_time) {
        if (window->GetKey(InputManager::exit_key).bPressed) {
            quit();
            return;
        }

        if (window->GetKey(InputManager::back_key).bPressed && scene != scene_list.get()) {
            play_sound(skin->get_random_menu_pluck());
            set_scene(scene_list.get());
        }

        limit_frame_rate(SettingsManager::settings.frame_rate_limit);
        begin_frame(elapsed_time);
        text_cache_begin_frame();
        handle_dropped_files();
        toast_timer = std::max(0.0f, toast_timer - elapsed_time);

        this->elapsed_time = elapsed_time;
        scene->update(elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        draw_scenes(window);

        if (SettingsManager::settings.show_fps) {
            uint16_t fps = elapsed_time > 0 ? (uint16_t)std::min(1 / elapsed_time, 65535.0f) : 0;
            draw_text(skin->display_font_small, std::format("FPS: {}", fps), window->GetScreenSize().x - 100, 30, olc::WHITE);
        }

        draw_toast(window);
    }

    void GameNascent::show_toast(std::string message, bool is_error, float duration) {
        toast = std::move(message);
        toast_is_error = is_error;
        toast_timer = duration;
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

        for (const std::string& file : get_frame_dropped_files()) {
            std::filesystem::path path = utf8_path(file);
            std::string extension = path_utf8(path.extension());

            for (char& c : extension) {
                c = (char)std::tolower((unsigned char)c);
            }

            if (extension != ".zip") {
                show_toast("Drop an Etterna noteskin .zip to import it", true);
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
        int32_t width = bounds.size.x + TOAST_PADDING * 2;
        int32_t height = metrics.size.y + TOAST_PADDING * 2;
        int32_t x = (window->GetScreenSize().x - width) / 2;
        float fade = std::min(1.0f, toast_timer / TOAST_FADE_TIME);

        olc::Pixel border = toast_is_error ? olc::Pixel(235, 80, 80, (uint8_t)(255 * fade)) : olc::Pixel(90, 220, 120, (uint8_t)(255 * fade));
        float left = (float)x;
        float top = (float)TOAST_TOP;
        float edge = (float)TOAST_BORDER;

        window->FillRectDecal({left, top}, {(float)width, (float)height}, olc::Pixel(0, 0, 0, (uint8_t)(220 * fade)));
        window->FillRectDecal({left, top}, {(float)width, edge}, border);
        window->FillRectDecal({left, top + height - edge}, {(float)width, edge}, border);
        window->FillRectDecal({left, top + edge}, {edge, height - edge * 2}, border);
        window->FillRectDecal({left + width - edge, top + edge}, {edge, height - edge * 2}, border);
        draw_text(font, toast, {x + TOAST_PADDING, TOAST_TOP + TOAST_PADDING - metrics.offset.y}, olc::Pixel(255, 255, 255, (uint8_t)(255 * fade)));
    }

    void GameNascent::discord_init() {
        discord::RPCManager::get()
            .setClientID(SettingsManager::settings.discord_application_id)
            .onReady([](discord::User const& user) {
                printf("Discord ready with user %s!\n", user.username.c_str());
            });

        discord::RPCManager::get().initialize();
    }

    void GameNascent::discord_update() {
        discord::RPCManager::get().getPresence().setState("Training Rhythm").setDetails("Offline play").setStartTimestamp(start_time).refresh();
    }
}
