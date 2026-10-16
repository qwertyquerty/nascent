#pragma once

#include "gse/input/Input.h"
#include "gse/ui/Theme.h"
#include "olcPixelGameEngine.h"

#include <cstdint>

namespace nascent {
    class UiField {
        public:
        virtual ~UiField() = default;

        virtual void on_focus() {}
        virtual void on_blur() {}
        virtual bool update(Input&, float) {
            return false;
        }
        virtual bool click(olc::vf2d, Input&) {
            return false;
        }
        virtual bool drag(olc::vf2d) {
            return false;
        }
        virtual int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const = 0;
        virtual bool is_filled() const {
            return true;
        }
        virtual bool consumes_navigation() const {
            return false;
        }
        virtual bool focusable() const {
            return true;
        }
    };
}
