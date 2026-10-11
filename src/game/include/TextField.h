#pragma once

#include "UiField.h"

#include <cstddef>
#include <string>

namespace nascent {
    class TextField : public UiField {
        protected:
        float blink_timer = 0;
        size_t cursor = 0;

        public:
        static constexpr int32_t WIDTH = 640;
        static constexpr int32_t HEIGHT = 46;
        static constexpr int32_t PADDING = 12;

        std::string label;
        std::string value;
        bool secret = false;

        TextField(std::string label, std::string value = "", bool secret = false);

        void on_focus(olc::PixelGameEngine* window) override;
        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
        int32_t draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position, bool focused) const override;
        bool is_filled() const override;
    };
}
