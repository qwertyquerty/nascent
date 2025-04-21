#include "GameNascent.h"

#include "olcPixelGameEngine.h"
#include "ScenePlay.h"
#include "SceneTitle.h"
#include "discord-rpc.hpp"

namespace nascent {
    GameNascent::GameNascent(olc::PixelGameEngine* window) : Game(window) {
        scene_title = new ScenePlay();
    };

    GameNascent::~GameNascent() {
        discord::RPCManager::get().shutdown();

        delete scene_title;
    };

    void GameNascent::init() {
        olc::Font::init();

        start_time = time(nullptr);
        std::srand(start_time);
        
        set_scene(scene_title);
        discord_init();
        discord_update();

        skin = new Skin(this, "default", 4);
    }

    void GameNascent::update(float elapsed_time) {
        this->elapsed_time = elapsed_time;
        scene->update(this, elapsed_time);
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
            .setClientID(DISCORD_APPLICATION_ID)
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
