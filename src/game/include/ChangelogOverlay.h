#pragma once

#include "Changelog.h"
#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
#include "gse/ui/ScrollBar.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nascent {
    class Skin;

    class ChangelogOverlay : public Overlay {
        protected:
        enum class LineKind : uint8_t {
            RELEASE,
            SECTION,
            ITEM,
            CONTINUATION
        };

        struct Line {
            LineKind kind;
            std::string text;
            float y;
        };

        Skin* skin;
        std::vector<ChangelogRelease> releases;
        std::string title;
        std::string subtitle;
        std::vector<Line> lines;
        float wrapped_width = -1.0f;
        float scroll = 0;
        float content_height = 0;
        float view_height = 0;
        ScrollBar scrollbar;
        Hotspots spots;

        void rebuild(float width);
        float max_scroll() const;
        olc::Font& font_for(LineKind kind) const;

        public:
        static constexpr const char* CLOSE_HINT = "Enter / Esc  close";
        static constexpr const char* EMPTY_TEXT = "There are no notes for this version yet.";

        ChangelogOverlay(Skin* skin, std::vector<ChangelogRelease> releases, std::string title, std::string subtitle);

        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        olc::Pixel backdrop() const override;
    };
}
