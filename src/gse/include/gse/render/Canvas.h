#pragma once

#include "olcPixelGameEngine.h"

#include <cstdint>

namespace nascent {
    inline void paint_layer(olc::PixelGameEngine* window, uint8_t layer, olc::Pixel color) {
        olc::Renderable& target = window->GetLayers()[layer].pDrawTarget;
        window->SetDrawTarget(layer, false);

        if (target.Sprite()->GetPixel(0, 0) != color) {
            window->Clear(color);
            target.Decal()->Update();
        }
    }
}
