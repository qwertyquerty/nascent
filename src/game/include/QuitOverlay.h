#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <vector>

namespace nascent {
    class Skin;

    class QuitOverlay : public Overlay {
        protected:
        Skin* skin;
        size_t selection = 0;
        olc::vi2d last_mouse = {-1, -1};
        std::vector<Rect> item_bounds;

        void choose(size_t choice);

        public:
        static constexpr std::array<const char*, 2> ITEMS = {"Quit", "Cancel"};
        static constexpr size_t QUIT = 0;
        static constexpr size_t CANCEL = 1;
        static constexpr float TITLE_TOP = 0.36f;
        static constexpr float ITEM_SPACING = 58.0f;
        static constexpr float ITEM_PADDING = 12.0f;
        static constexpr float HINT_BOTTOM = 80.0f;

        explicit QuitOverlay(Skin* skin);

        void on_open() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
    };
}
