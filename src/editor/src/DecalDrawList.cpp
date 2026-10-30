#include "DecalDrawList.h"

#include "imgui.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>

namespace nascent {
    namespace {
        void additive_blend(const ImDrawList*, const ImDrawCmd*) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        }

        void normal_blend(const ImDrawList*, const ImDrawCmd*) {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        size_t triangle_count(const olc::DecalInstance& decal, size_t points) {
            switch (decal.structure) {
                case olc::DecalStructure::FAN:
                case olc::DecalStructure::STRIP:
                    return points >= 3 ? points - 2 : 0;
                case olc::DecalStructure::LIST:
                    return points / 3;
                default:
                    return 0;
            }
        }

        std::array<size_t, 3> triangle(const olc::DecalInstance& decal, size_t index) {
            switch (decal.structure) {
                case olc::DecalStructure::FAN:
                    return {0, index + 1, index + 2};
                case olc::DecalStructure::STRIP:
                    return {index, index + 1, index + 2};
                default:
                    return {index * 3, index * 3 + 1, index * 3 + 2};
            }
        }

        void add_decal(ImDrawList* list, const olc::DecalInstance& decal, olc::vi2d screen, const olc::Decal* blank) {
            size_t points = std::min({(size_t)decal.points, decal.pos.size(), decal.uv.size(), decal.tint.size()});
            size_t triangles = triangle_count(decal, points);

            if (triangles == 0) {
                return;
            }

            const olc::Decal* texture = decal.decal != nullptr ? decal.decal : blank;
            list->PushTexture(ImTextureRef((ImTextureID)(uint32_t)texture->id));
            list->PrimReserve((int)(triangles * 3), (int)points);
            ImDrawIdx base = (ImDrawIdx)list->_VtxCurrentIdx;

            for (size_t point = 0; point < points; point++) {
                float w = point < decal.w.size() && decal.w[point] != 0.0f ? decal.w[point] : 1.0f;
                const olc::Pixel& tint = decal.tint[point];
                ImVec2 position = {(decal.pos[point].x + 1.0f) * 0.5f * (float)screen.x, (1.0f - decal.pos[point].y) * 0.5f * (float)screen.y};
                ImVec2 uv = decal.decal != nullptr ? ImVec2(decal.uv[point].x / w, decal.uv[point].y / w) : ImVec2(0.5f, 0.5f);
                list->PrimWriteVtx(position, uv, IM_COL32(tint.r, tint.g, tint.b, tint.a));
            }

            for (size_t index = 0; index < triangles; index++) {
                for (size_t corner : triangle(decal, index)) {
                    list->PrimWriteIdx((ImDrawIdx)(base + corner));
                }
            }

            list->PopTexture();
        }
    }

    void draw_decals(ImDrawList* list, const std::vector<olc::DecalInstance>& decals, olc::vi2d screen, const olc::Decal* blank) {
        bool additive = false;

        for (const olc::DecalInstance& decal : decals) {
            bool wants_additive = decal.mode == olc::DecalMode::ADDITIVE;

            if (wants_additive != additive) {
                list->AddCallback(wants_additive ? additive_blend : normal_blend);
                additive = wants_additive;
            }

            add_decal(list, decal, screen, blank);
        }

        if (additive) {
            list->AddCallback(normal_blend);
        }
    }
}
