#pragma once

#include "olcPixelGameEngine.h"

#include <memory>
#include <vector>

namespace nascent {
    class Game;

    class Overlay {
        protected:
        Game* game = nullptr;

        public:
        virtual ~Overlay() = default;
        virtual void on_open() {}
        virtual void update(float elapsed_time) = 0;
        virtual void draw(olc::PixelGameEngine* window) = 0;
        virtual bool is_modal() const;
        virtual olc::Pixel backdrop() const;

        void close();
        bool is_closed() const;

        private:
        friend class OverlayStack;
        bool closed = false;
    };

    class OverlayStack {
        protected:
        Game* game = nullptr;
        std::vector<std::unique_ptr<Overlay>> overlays;

        public:
        void bind(Game* game);

        template <typename T>
        T& open(std::unique_ptr<T> overlay) {
            T& opened = *overlay;
            add(std::move(overlay));
            return opened;
        }

        template <typename T>
        T* find() const {
            for (const std::unique_ptr<Overlay>& overlay : overlays) {
                if (T* match = dynamic_cast<T*>(overlay.get()); match != nullptr && !match->is_closed()) {
                    return match;
                }
            }

            return nullptr;
        }

        void update(float elapsed_time);
        void draw(olc::PixelGameEngine* window) const;
        bool has_modal() const;
        bool empty() const;
        void clear();

        private:
        void add(std::unique_ptr<Overlay> overlay);
        void remove_closed();
    };
}
