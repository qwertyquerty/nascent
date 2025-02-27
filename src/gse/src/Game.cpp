#include "Game.h"

namespace nascent {
    Game::Game() {};

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
