#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"

namespace nascent {
    enum class UiFont {
        SMALL,
        MEDIUM,
        LARGE
    };

    enum class UiColor {
        TEXT,
        TEXT_MUTED,
        TEXT_FAINT,
        INPUT_BACKGROUND,
        INPUT_BACKGROUND_FOCUSED,
        PROGRESS_TRACK,
        SEPARATOR
    };

    class UiTheme {
        public:
        virtual ~UiTheme() = default;
        virtual olc::Font& font(UiFont font) = 0;
        virtual olc::Pixel color(UiColor color) const = 0;
        virtual olc::Pixel accent(double position) const = 0;
    };
}
