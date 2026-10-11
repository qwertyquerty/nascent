#pragma once

#include "Results.h"
#include "olcPixelGameEngine.h"

namespace nascent {
    inline olc::Pixel grade_color(Grade grade) {
        switch (grade) {
            case Grade::X:
                return olc::Pixel(255, 240, 160);
            case Grade::S:
                return olc::Pixel(255, 210, 80);
            case Grade::A:
                return olc::Pixel(110, 220, 120);
            case Grade::B:
                return olc::Pixel(90, 170, 255);
            case Grade::C:
                return olc::Pixel(190, 120, 255);
            case Grade::D:
                return olc::Pixel(255, 150, 70);
            case Grade::E:
                return olc::Pixel(240, 90, 90);
            default:
                return olc::Pixel(150, 150, 150);
        }
    }
}
