#include "GameNascent.h"

#include "olcPixelGameEngine.h"

namespace nascent {
    GameNascent::GameNascent() {
        scene_title = new SceneTitle();
    };

    GameNascent::~GameNascent() {
        delete scene_title;
    };

    void GameNascent::init() {
        set_scene(scene_title);
    }

    void GameNascent::update(float elapsed_time) {
        scene->update(this, elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        window->Clear(olc::BLACK);
        scene->draw(window);
    }
}
