#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <vector>

namespace nascent {
    class ScenePlay;
    class Skin;

    class PauseOverlay : public Overlay {
        protected:
        ScenePlay& play;
        Skin* skin;
        size_t selection = 0;
        olc::vi2d last_mouse = {-1, -1};
        std::vector<Rect> item_bounds;

        void choose(size_t choice);

        public:
        static constexpr std::array<const char*, 3> ITEMS = {"Continue", "Restart", "Quit to song list"};
        static constexpr size_t CONTINUE = 0;
        static constexpr size_t RESTART = 1;
        static constexpr size_t QUIT = 2;
        static constexpr float TITLE_TOP = 0.26f;
        static constexpr float ITEM_SPACING = 58.0f;
        static constexpr float ITEM_PADDING = 12.0f;
        static constexpr float HINT_BOTTOM = 80.0f;

        PauseOverlay(ScenePlay& play, Skin* skin);

        void on_open() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
    };
}
