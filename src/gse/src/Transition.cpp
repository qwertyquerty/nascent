#include "gse/Transition.h"

#include "gse/anim/Easing.h"
#include "gse/render/Canvas.h"

#include <algorithm>
#include <cmath>

namespace nascent {
    static void make_layer_blank(olc::PixelGameEngine* window, uint32_t layer) {
        window->GetLayers()[layer].pDrawTarget.Create(1, 1);
    }

    FadeTransition::FadeTransition(float seconds) : seconds(std::max(0.0f, seconds)) {}

    float FadeTransition::duration() const {
        return seconds;
    }

    void FadeTransition::draw(olc::PixelGameEngine* window, float progress, olc::Pixel clear_color, const std::function<void()>& draw_outgoing, const std::function<void()>& draw_incoming) {
        prepare(window);
        window->EnableLayer((uint8_t)layer, true);
        paint_layer(window, (uint8_t)layer, clear_color);
        draw_outgoing();
        window->SetDecalMode(olc::DecalMode::NORMAL);
        window->FillRectDecal({0.0f, 0.0f}, {1.0f, 1.0f}, olc::BLANK);

        paint_layer(window, 0, clear_color);
        draw_incoming();

        uint8_t alpha = (uint8_t)std::lround(smooth(progress) * 255);
        window->SetLayerTint(0, olc::Pixel(255, 255, 255, alpha));

        if (alpha < 255) {
            for (olc::DecalInstance& decal : window->GetLayers()[0].vecDecalInstance) {
                for (olc::Pixel& tint : decal.tint) {
                    tint.a = (uint8_t)(tint.a * alpha / 255);
                }
            }
        }
    }

    void FadeTransition::prepare(olc::PixelGameEngine* window) {
        if (layer >= 0) {
            return;
        }

        olc::LayerDesc desc;
        desc.pDrawTarget.Create(1, 1);
        window->GetLayers().push_back(std::move(desc));
        layer = (int32_t)window->GetLayers().size() - 1;
    }

    void FadeTransition::idle(olc::PixelGameEngine* window) {
        if (layer >= 0) {
            window->EnableLayer((uint8_t)layer, false);
        }

        window->SetLayerTint(0, olc::WHITE);
    }

    void FadeTransition::resize(olc::PixelGameEngine* window) {
        if (layer >= 0) {
            make_layer_blank(window, (uint32_t)layer);
        }
    }
}
