#pragma once

#include "olcPixelGameEngine.h"

#include <cstdint>
#include <string>

namespace nascent {
    class Skin;

    class UiField {
        public:
        virtual ~UiField() = default;

        virtual void on_focus(olc::PixelGameEngine*) {}
        virtual void on_blur(olc::PixelGameEngine*) {}
        virtual bool update(olc::PixelGameEngine*, const std::string&, float) {
            return false;
        }
        virtual int32_t draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position, bool focused) const = 0;
        virtual bool is_filled() const {
            return true;
        }
    };
}
