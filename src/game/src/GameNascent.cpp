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
#include "TextCache.h"

#include <algorithm>
#include <ctime>
#include <format>

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

        this->elapsed_time = elapsed_time;
        scene->update(elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        draw_scenes(window);

        if (SettingsManager::settings.show_fps) {
            uint16_t fps = elapsed_time > 0 ? (uint16_t)std::min(1 / elapsed_time, 65535.0f) : 0;
            draw_text(skin->display_font_small, std::format("FPS: {}", fps), window->GetScreenSize().x - 100, 30, olc::WHITE);
        }
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
