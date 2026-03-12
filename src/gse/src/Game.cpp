#include "Game.h"

namespace nascent {
    Game::Game(olc::PixelGameEngine* window, Scene* scene) {
        this->window = window;
        this->set_scene(scene);
        this->try_scene_swap();
    };

    void Game::set_scene(Scene* scene) {
        this->queued_scene = scene;
    }

    Scene* Game::get_scene() {
        return this->scene;
    }

    void Game::try_scene_swap() {
        if (this->queued_scene != nullptr) {
            if (this->scene != nullptr) {
                delete this->scene;
            }

            this->scene = this->queued_scene;
            this->queued_scene = nullptr;
            scene->set_game(this);
            scene->init(this);
        }
    }

    olc::MiniAudio& Game::get_audio() {
        return this->audio;
    }
}
