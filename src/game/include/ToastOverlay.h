#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"

#include <cstdint>
#include <functional>
#include <string>

namespace nascent {
    class Skin;

    class ToastOverlay : public Overlay {
        protected:
        Skin* skin;
        std::string message;
        float timer = 0;
        bool is_error = false;
        std::function<void()> action;
        Rect area;

        public:
        static constexpr float DEFAULT_TIME = 5.0f;
        static constexpr float FADE_TIME = 0.5f;
        static constexpr int32_t PADDING = 16;
        static constexpr int32_t TOP = 60;
        static constexpr int32_t BORDER = 2;

        explicit ToastOverlay(Skin* skin);

        void show(std::string message, bool is_error, float duration = DEFAULT_TIME, std::function<void()> action = {});
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        bool is_modal() const override;
    };
}
