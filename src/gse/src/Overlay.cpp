#include "gse/Overlay.h"

#include <algorithm>

namespace nascent {
    bool Overlay::is_modal() const {
        return true;
    }

    olc::Pixel Overlay::backdrop() const {
        return olc::BLANK;
    }

    void Overlay::close() {
        closed = true;
    }

    bool Overlay::is_closed() const {
        return closed;
    }

    void OverlayStack::bind(Game* game) {
        this->game = game;

        for (std::unique_ptr<Overlay>& overlay : overlays) {
            overlay->game = game;
        }
    }

    void OverlayStack::add(std::unique_ptr<Overlay> overlay) {
        overlay->game = game;
        overlays.push_back(std::move(overlay));
        overlays.back()->on_open();
    }

    void OverlayStack::remove_closed() {
        std::erase_if(overlays, [](const std::unique_ptr<Overlay>& overlay) { return overlay->is_closed(); });
    }

    void OverlayStack::update(float elapsed_time) {
        remove_closed();
        bool modal_seen = false;
        size_t count = overlays.size();

        for (size_t index = count; index-- > 0;) {
            Overlay* overlay = overlays[index].get();

            if (overlay->is_closed()) {
                continue;
            }

            if (overlay->is_modal()) {
                if (modal_seen) {
                    continue;
                }

                modal_seen = true;
            }

            overlay->update(elapsed_time);
        }

        remove_closed();
    }

    void OverlayStack::draw(olc::PixelGameEngine* window) const {
        olc::vf2d screen = window->GetScreenSize();

        for (const std::unique_ptr<Overlay>& overlay : overlays) {
            if (overlay->is_closed()) {
                continue;
            }

            if (olc::Pixel backdrop = overlay->backdrop(); backdrop.a > 0) {
                window->FillRectDecal({0.0f, 0.0f}, screen, backdrop);
            }

            overlay->draw(window);
        }
    }

    bool OverlayStack::has_modal() const {
        return std::any_of(overlays.begin(), overlays.end(), [](const std::unique_ptr<Overlay>& overlay) { return !overlay->is_closed() && overlay->is_modal(); });
    }

    bool OverlayStack::empty() const {
        return std::none_of(overlays.begin(), overlays.end(), [](const std::unique_ptr<Overlay>& overlay) { return !overlay->is_closed(); });
    }

    void OverlayStack::clear() {
        overlays.clear();
    }
}
