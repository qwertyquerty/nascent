#include "EditorField.h"

#include "Chart.h"

#include <cmath>

namespace nascent {
    double EditorField::lane_left(uint8_t column) const {
        return pos.x + column * (note_size + note_x_spacing);
    }

    double EditorField::lane_width() const {
        return note_size;
    }

    double EditorField::note_height() const {
        return note_size;
    }

    double EditorField::judge_top() const {
        return std::floor(pos.y + size.y - note_size - judge_height);
    }

    double EditorField::rate(const olc::PixelGameEngine* window) const {
        return (double)scroll_speed * window->ScreenHeight() / 100.0 / 1000.0;
    }

    double EditorField::note_top(double time, const olc::PixelGameEngine* window) const {
        return judge_top() - std::trunc((time - position) * rate(window));
    }

    double EditorField::time_at(double y, const olc::PixelGameEngine* window) const {
        double per_ms = rate(window);
        return per_ms > 0 ? position + (judge_top() + note_size / 2 - y) / per_ms : position;
    }

    std::optional<uint8_t> EditorField::column_at(double x) const {
        uint8_t keys = chart->info.key_count;

        for (uint8_t column = 0; column < keys; column++) {
            double left = lane_left(column);
            double right = column + 1 < keys ? lane_left((uint8_t)(column + 1)) : left + note_size;

            if (x >= left && x < right) {
                return column;
            }
        }

        return std::nullopt;
    }

    bool EditorField::contains(olc::vf2d point) const {
        return point.x >= pos.x && point.x < pos.x + size.x && point.y >= pos.y && point.y < pos.y + size.y;
    }
}
