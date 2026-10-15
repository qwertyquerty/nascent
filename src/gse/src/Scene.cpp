#include "gse/Scene.h"

namespace nascent {
    bool Scene::is_created() const {
        return created;
    }

    void Scene::attach(Game* game) {
        this->game = game;
        overlays.bind(game);

        if (!created) {
            created = true;
            on_create();
        }
    }
}
