#include "GameNascent.h"

#include "olcPixelGameEngine.h"

namespace nascent {
    GameNascent::GameNascent(olc::PixelGameEngine* window) : Game(window) {
        scene_title = new SceneTitle();
    };

    GameNascent::~GameNascent() {
        delete scene_title;
    };

    void GameNascent::init() {
        set_scene(scene_title);
    }

    void GameNascent::update(float elapsed_time) {
        this->elapsed_time = elapsed_time;
        scene->update(this, elapsed_time);
    }

    void GameNascent::draw(olc::PixelGameEngine* window) {
        window->Clear(olc::BLACK);
        scene->draw(window);

        if (debug) {
            window->DrawString(window->GetWindowSize().x - 300, 10, std::format("FT {}", elapsed_time * 1000), olc::WHITE, 2);
        }
    }
}
