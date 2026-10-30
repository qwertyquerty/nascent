#pragma once

#include "olcPixelGameEngine.h"

#include <vector>

struct ImDrawList;

namespace nascent {
    void draw_decals(ImDrawList* list, const std::vector<olc::DecalInstance>& decals, olc::vi2d screen, const olc::Decal* blank);
}
