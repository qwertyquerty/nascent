#include "Scene.h"

namespace nascent {
    Scene::Scene() {
        entities = std::vector<Entity>();
    }

    void Scene::set_game(Game* game) {
        this->game = game;
    }
}
