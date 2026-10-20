#include "TimingBar.h"

#include "Accuracy.h"
#include "Attempt.h"
#include "Skin.h"

#include <algorithm>
#include <cstdint>

namespace nascent {
    static constexpr uint8_t MAX_HIT_LINES = 100;

    void draw_timing_bar(olc::PixelGameEngine* window, const Skin* skin, const Attempt& attempt, float center_x, float length, float thickness, float height) {
        olc::vi2d screensize = window->GetScreenSize();
        float hit_width = (float)(int32_t)(screensize.x * skin->layout("timing_bar.hit_width_fraction"));
        float hit_height = thickness;
        length = std::min(length, screensize.x * TIMING_BAR_MAX_LENGTH / 100.0f);
        float px_per_ms = length / get_window_from_hit_acc_diff(HitScore::BAD, attempt.hit_accuracy) / 2;
        float y = (1 - height) * (screensize.y - hit_height);
        uint8_t window_alpha = (uint8_t)skin->layout("timing_bar.window_alpha");

        for (auto it = TIMED_HIT_SCORES.rbegin(); it != TIMED_HIT_SCORES.rend(); ++it) {
            uint16_t hit_err_window = get_window_from_hit_acc_diff(*it, attempt.hit_accuracy);
            olc::Pixel c = skin->judgement_color(*it);
            c.a = window_alpha;
            window->FillRectDecal({center_x - hit_err_window * px_per_ms, y}, {hit_err_window * 2 * px_per_ms, hit_height}, c);
        }

        olc::Pixel hit_color = skin->color(SkinColor::TIMING_BAR_HIT);
        uint8_t lines = 0;

        for (int64_t i = attempt.soonest_scored_hit_index; i >= 0 && i < (int64_t)attempt.judged_hits.size() && lines < MAX_HIT_LINES; i--) {
            const JudgedHit& jhit = attempt.judged_hits[i];

            if (jhit.hit_played && jhit.hit_score != HitScore::MISS) {
                float x = center_x + jhit.hit_err * px_per_ms - hit_width / 2;
                olc::Pixel line = olc::Pixel(hit_color.r, (uint8_t)std::max(0, hit_color.g - lines), (uint8_t)std::max(0, hit_color.b - lines), (uint8_t)(hit_color.a * (MAX_HIT_LINES - lines) / 255));
                window->FillRectDecal({x, y}, {hit_width, hit_height}, line);
                lines++;
            }
        }
    }
}
