#include "GameNascent.h"

#include "InputManager.h"
#include "olcPixelGameEngine.h"
#include "ScenePlay.h"
#include "SceneTitle.h"
#include "SceneList.h"
#include "discord-rpc.hpp"
#include "SettingsManager.h"
#include "InputManager.h"

namespace nascent {
    GameNascent::GameNascent(olc::PixelGameEngine* window) : Game(window, nullptr) {

    };

    GameNascent::~GameNascent() {
        discord::RPCManager::get().shutdown();
        delete scene_list;
    };

    void GameNascent::init() {
        olc::Font::init();

        scene_list = new SceneList();
        set_scene(scene_list);
        try_scene_swap();

        start_time = time(nullptr);
        std::srand(start_time);
        
        discord_init();
        discord_update();

        skin = new Skin(this, "default", 7);
    }

    void GameNascent::update(float elapsed_time) {
        if (window->GetKey(InputManager::back_key).bPressed) {
            set_scene(scene_list);
        }

        this->elapsed_time = elapsed_time;
        scene->update(elapsed_time);

        if (window->GetKey(olc::Key::CTRL).bHeld) {
            if (window->GetKey(InputManager::ctrl_exit_key).bPressed) {
                delete this;
                exit(0);
            }
        }
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        window->Clear(olc::BLACK);
        scene->draw(window);

        if (debug) {
            skin->display_font_small.DrawString(std::format("FPS: {}", (uint16_t)(1/elapsed_time)), window->GetWindowSize().x - 100, 30, olc::WHITE);
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
        discord::RPCManager::get().getPresence()
            .setState("Training Rhythm")
            .setDetails("Offline play")
            .setStartTimestamp(start_time)
            .refresh();
    }
}
