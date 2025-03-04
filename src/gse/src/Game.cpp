#include "Game.h"

namespace nascent {
    Game::Game(olc::PixelGameEngine* window) {
        this->window = window;
    };

    void Game::set_scene(Scene* scene) {
        scene->init(this);
        this->scene = scene;
    }

    Scene* Game::get_scene() {
        return this->scene;
    }

    olc::MiniAudio& Game::get_audio() {
        return this->audio;
    }
}
