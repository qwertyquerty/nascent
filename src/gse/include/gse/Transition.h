#pragma once

#include "olcPixelGameEngine.h"

#include <cstdint>
#include <functional>

namespace nascent {
    class Transition {
        public:
        virtual ~Transition() = default;
        virtual float duration() const = 0;
        virtual void draw(olc::PixelGameEngine* window, float progress, olc::Pixel clear_color, const std::function<void()>& draw_outgoing, const std::function<void()>& draw_incoming) = 0;
        virtual void prepare(olc::PixelGameEngine*) {}
        virtual void idle(olc::PixelGameEngine*) {}
        virtual void resize(olc::PixelGameEngine*) {}
    };

    class FadeTransition : public Transition {
        protected:
        float seconds;
        int32_t layer = -1;

        public:
        explicit FadeTransition(float seconds);

        float duration() const override;
        void draw(olc::PixelGameEngine* window, float progress, olc::Pixel clear_color, const std::function<void()>& draw_outgoing, const std::function<void()>& draw_incoming) override;
        void prepare(olc::PixelGameEngine* window) override;
        void idle(olc::PixelGameEngine* window) override;
        void resize(olc::PixelGameEngine* window) override;
    };
}
