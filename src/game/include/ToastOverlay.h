#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"

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

        explicit ToastOverlay(Skin* skin);

        void show(std::string message, bool is_error, float duration = DEFAULT_TIME, std::function<void()> action = {});
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        bool is_modal() const override;
    };
}
