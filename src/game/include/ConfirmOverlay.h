#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace nascent {
    class Skin;

    class ConfirmOverlay : public Overlay {
        protected:
        Skin* skin;
        std::string title;
        std::vector<std::string> lines;
        std::array<std::string, 2> items;
        std::function<void()> on_confirm;
        size_t selected = 0;
        Hotspots spots;
        Rect panel;
        olc::vi2d last_mouse = {-1, -1};

        void choose(size_t choice);

        public:
        static constexpr size_t CONFIRM = 0;
        static constexpr size_t CANCEL = 1;

        ConfirmOverlay(Skin* skin, std::string title, std::vector<std::string> lines, std::string confirm_label, std::function<void()> on_confirm);

        void on_open() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        olc::Pixel backdrop() const override;
    };
}
