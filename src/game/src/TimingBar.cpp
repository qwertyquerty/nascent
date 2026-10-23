#include "TimingBar.h"

#include "Accuracy.h"
#include "Attempt.h"
#include "Skin.h"
#include "Chart.h"
#include "gse/render/MipDecal.h"

#include <algorithm>
#include <cstdint>

namespace nascent {
    static constexpr uint8_t MAX_HIT_LINES = 100;

    void draw_skin_image(olc::PixelGameEngine* window, const MipDecal* image, olc::vf2d position, olc::vf2d size, olc::Pixel tint, float fraction) {
        if (image == nullptr || image->size().x <= 0 || image->size().y <= 0 || size.x <= 0 || size.y <= 0) {
            return;
        }

        fraction = std::clamp(fraction, 0.0f, 1.0f);
        MipDecal::Pick level = image->pick(size.x / image->size().x);
        olc::vf2d source = {(float)level.decal->sprite->width, (float)level.decal->sprite->height};
        window->DrawPartialDecal(position, {size.x * fraction, size.y}, level.decal, {0.0f, 0.0f}, {source.x * fraction, source.y}, tint);
    }

    void draw_timing_bar(olc::PixelGameEngine* window, Skin* skin, const Attempt& attempt, float center_x, float length, float thickness, float height, bool show_average) {
        olc::vi2d screensize = window->GetScreenSize();
        float hit_width = (float)(int32_t)(screensize.x * skin->layout("timing_bar.hit_width_fraction"));
        float hit_height = thickness;
        length = std::min(length, screensize.x * TIMING_BAR_MAX_LENGTH / 100.0f);
        float px_per_ms = length / get_window_from_hit_acc_diff(HitScore::BAD, attempt.hit_accuracy) / 2;
        float y = (1 - height) * (screensize.y - hit_height);
        uint8_t window_alpha = (uint8_t)skin->layout("timing_bar.window_alpha");
        uint8_t key_count = attempt.chart != nullptr ? attempt.chart->info.key_count : 0;

        if (const MipDecal* background = skin->image(SkinImage::TIMING_BAR_BACKGROUND, 0, key_count)) {
            float span = get_window_from_hit_acc_diff(HitScore::BAD, attempt.hit_accuracy) * px_per_ms;
            draw_skin_image(window, background, {center_x - span, y}, {span * 2, hit_height}, olc::WHITE);
        }
        else {
            for (auto it = TIMED_HIT_SCORES.rbegin(); it != TIMED_HIT_SCORES.rend(); ++it) {
                uint16_t hit_err_window = get_window_from_hit_acc_diff(*it, attempt.hit_accuracy);
                olc::Pixel c = skin->judgement_color(*it);
                c.a = window_alpha;
                window->FillRectDecal({center_x - hit_err_window * px_per_ms, y}, {hit_err_window * 2 * px_per_ms, hit_height}, c);
            }
        }

        const MipDecal* hit_image = skin->image(SkinImage::TIMING_BAR_HIT, 0, key_count);
        uint8_t lines = 0;
        uint32_t average_hits = std::max<uint32_t>(1, (uint32_t)skin->layout("timing_bar.average_hits"));
        uint32_t averaged = 0;
        int64_t error_sum = 0;

        for (int64_t i = attempt.soonest_scored_hit_index; i >= 0 && i < (int64_t)attempt.judged_hits.size() && (lines < MAX_HIT_LINES || (show_average && averaged < average_hits)); i--) {
            const JudgedHit& jhit = attempt.judged_hits[i];

            if (!jhit.hit_played || jhit.hit_score == HitScore::MISS) {
                continue;
            }

            if (averaged < average_hits) {
                error_sum += jhit.hit_err;
                averaged++;
            }

            if (lines < MAX_HIT_LINES) {
                float x = center_x + jhit.hit_err * px_per_ms - hit_width / 2;
                olc::Pixel hit_color = skin->color(jhit.hit_err < 0 ? SkinColor::TIMING_EARLY : SkinColor::TIMING_LATE);
                olc::Pixel line = olc::Pixel(hit_color.r, (uint8_t)std::max(0, hit_color.g - lines), (uint8_t)std::max(0, hit_color.b - lines), (uint8_t)(hit_color.a * (MAX_HIT_LINES - lines) / 255));

                if (hit_image != nullptr) {
                    draw_skin_image(window, hit_image, {x, y}, {hit_width, hit_height}, line);
                }
                else {
                    window->FillRectDecal({x, y}, {hit_width, hit_height}, line);
                }

                lines++;
            }
        }

        if (!show_average || averaged == 0) {
            return;
        }

        float average_x = center_x + std::clamp((float)error_sum / averaged * px_per_ms, -length / 2, length / 2);
        float marker = std::max(2.0f, (float)(screensize.y * skin->layout("timing_bar.average_size_fraction")));
        float half_width = marker * (float)skin->layout("timing_bar.average_width_ratio") / 2;
        float gap = std::max(1.0f, marker / 4);
        bool below = y - gap - marker < 0;
        float base = below ? y + hit_height + gap + marker : y - gap - marker;
        float tip = below ? y + hit_height + gap : y - gap;

        if (const MipDecal* marker_image = skin->image(SkinImage::TIMING_BAR_AVERAGE, 0, key_count)) {
            draw_skin_image(window, marker_image, {average_x - half_width, std::min(base, tip)}, {half_width * 2, marker}, olc::WHITE);
            return;
        }

        window->FillTriangleDecal({average_x - half_width, base}, {average_x + half_width, base}, {average_x, tip}, skin->color(SkinColor::TEXT));
    }
}
