#include "GameNascent.h"

#include "Account.h"
#include "InputManager.h"
#include "olcPixelGameEngine.h"
#include "SceneAccount.h"
#include "SceneList.h"
#include "discord-rpc.hpp"
#include "SettingsManager.h"
#include "Skin.h"

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
    };

    void GameNascent::init() {
        SettingsManager::load();
        Account::load();
        scene_transition_time = SettingsManager::settings.scene_transition_time;

        olc::Font::init();

        init_input();

        start_time = time(nullptr);
        std::srand(start_time);

        skin = std::make_unique<Skin>(this, "default", 7);
        scene_list = std::make_unique<SceneList>(skin.get());

        if (Account::signed_in()) {
            set_scene(scene_list.get());
        }
        else {
            set_scene(std::make_unique<SceneAccount>(skin.get(), scene_list.get()));
        }

        discord_init();
        discord_update();
    }

    void GameNascent::update(float elapsed_time) {
        if (window->GetKey(InputManager::exit_key).bPressed) {
            quit();
            return;
        }

        if (window->GetKey(InputManager::back_key).bPressed) {
            set_scene(scene_list.get());
        }

        begin_frame(elapsed_time);

        this->elapsed_time = elapsed_time;
        scene->update(elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        draw_scenes(window);

        if (debug) {
            uint16_t fps = elapsed_time > 0 ? (uint16_t)std::min(1 / elapsed_time, 65535.0f) : 0;
            skin->display_font_small.DrawString(std::format("FPS: {}", fps), window->GetWindowSize().x - 100, 30, olc::WHITE);
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
