#pragma once

#include "EntityField.h"

#include <cstdint>
#include <optional>

namespace nascent {
    class EditorField : public EntityField {
        public:
        using EntityField::EntityField;

        double lane_left(uint8_t column) const;
        double lane_width() const;
        double note_height() const;
        double judge_top() const;
        double rate(const olc::PixelGameEngine* window) const;
        double note_top(double time, const olc::PixelGameEngine* window) const;
        double time_at(double y, const olc::PixelGameEngine* window) const;
        std::optional<uint8_t> column_at(double x) const;
        bool contains(olc::vf2d point) const;
    };
}
