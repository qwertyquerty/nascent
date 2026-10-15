#pragma once

#include "gse/Overlay.h"
#include "olcPixelGameEngine.h"

namespace nascent {
    class Game;

    class Scene {
        protected:
        Game* game = nullptr;

        public:
        OverlayStack overlays;

        virtual ~Scene() = default;
        virtual void on_create() {}
        virtual void on_enter() {}
        virtual void on_exit() {}
        virtual void on_destroy() {}
        virtual void on_resize() {}
        virtual void update(float elapsed_time) = 0;
        virtual void draw(olc::PixelGameEngine* window) = 0;

        bool is_created() const;

        private:
        friend class SceneStack;
        bool created = false;

        void attach(Game* game);
    };
}
