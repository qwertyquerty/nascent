#include "Scene.h"

namespace nascent {
    Scene::Scene() {
    }

    void Scene::set_game(Game* game) {
        this->game = game;
    }
}
