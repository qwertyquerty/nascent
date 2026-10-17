#include "EntityField.h"
#include "Attempt.h"
#include "Chart.h"
#include "SettingsManager.h"
#include "Skin.h"

#include "olcPixelGameEngine.h"
#include "gse/render/SpriteDecal.h"
#include "gse/anim/Easing.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace nascent {
    static constexpr float HOLD_EDGE_INSET = 0.5f;

    EntityField::EntityField(const Chart* chart, Skin* skin, olc::vd2d pos, olc::vd2d size) {
        this->chart = chart;
        this->skin = skin;
        this->pos = pos;
        this->size = size;

        scroll_speed = SettingsManager::settings.field_scroll_speed;
        set_lane_gap(std::nullopt);
        active_keys = 0;
    };

    void EntityField::set_lane_gap(std::optional<double> gap) {
        double keys = chart->info.key_count;

        if (gap) {
            note_x_spacing = keys > 1 ? std::clamp(*gap, 0.0, size.x / (keys - 1)) : 0.0;
            note_size = std::max(1.0, (size.x - note_x_spacing * (keys - 1)) / keys);
            return;
        }

        double spacing = skin->manifest().key_spacing;
        note_size = size.x / (keys + keys * spacing - spacing);
        note_x_spacing = note_size * spacing;
    }

    void EntityField::draw(olc::PixelGameEngine* window) {
        int32_t screen_height = window->ScreenHeight();
        const uint8_t key_count = chart->info.key_count;
        const SkinManifest& manifest = skin->manifest();

        // Draw judge line
        int32_t jl_y = pos.y + size.y - note_size - judge_height;

        if (draw_judge) {
            for (uint8_t column = 0; column < key_count; column++) {
                int32_t jl_x = pos.x + (column * (note_size + note_x_spacing));

                bool held = get_key(display_column(column));
                olc::Decal* receptor = sized_image(held ? SkinImage::RECEPTOR_PRESSED : SkinImage::RECEPTOR, column, key_count);
                olc::Pixel color = manifest.tint == SkinTint::GRADIENT && draw_judge_colors && note_gradient ? skin->title_gradient_at((double)(jl_x - pos.x) / size.x, (flip_y(jl_y) - pos.y) / size.y) : olc::Pixel(255, 255, 255);
                color.a = held ? 255 : manifest.receptor_alpha.value_or(judge_alpha);

                draw_square(window, receptor, jl_x, jl_y, with_opacity(color));
            }
        }

        double scroll_rate_px_per_ms = ((double)scroll_speed * screen_height / 100.0 / 1000.0);

        update_longest_hold();
        double field_ms = scroll_rate_px_per_ms > 0 ? size.y / scroll_rate_px_per_ms : 0;
        double earliest_time = position - longest_hold_ms - field_ms;
        auto first_hit = std::lower_bound(chart->hits.begin(), chart->hits.end(), earliest_time, [](const Hit& hit, double time) {
            return hit.time < time;
        });

        // Draw notes
        for (size_t i = (size_t)(first_hit - chart->hits.begin()); i < chart->hits.size(); i++) {
            const Hit& hit = chart->hits[i];
            const JudgedHit* judged = attempt != nullptr ? &attempt->judged_hits[i] : nullptr;
            bool hit_played = judged != nullptr && judged->hit_played;
            bool holding = hit_played && hit.hit_type == HitType::HOLD && !judged->released;
            bool dropped = hit_played && judged->released && judged->release_played && judged->release_err < 0 && judged->release_score == HitScore::MISS;
            bool stops_at_cut = !draw_notes_past_judge || hit_played;
            double cut_y = dropped ? jl_y - (judged->attempt_hit.end_time - position) * scroll_rate_px_per_ms : (double)jl_y;

            int32_t hit_y = (hit.time - position) * scroll_rate_px_per_ms;
            uint8_t column = display_column(hit.key);
            int32_t hit_x = pos.x + (column * (note_size + note_x_spacing));
            int32_t hit_height = hit.hit_type == HitType::HOLD ? (hit.end_time - hit.time) * scroll_rate_px_per_ms : 0;

            int32_t screen_hit_y = jl_y - hit_y;

            if ((screen_hit_y + note_size) < pos.y) {
                break;
            }

            if ((screen_hit_y - hit_height) < pos.y + size.y) {
                bool textured_body = manifest.hold_style == HoldStyle::STRETCH || manifest.hold_style == HoldStyle::TILE;
                olc::Decal* hold_body = hit.hit_type == HitType::HOLD && textured_body ? sized_image(SkinImage::HOLD_BODY, column, key_count) : nullptr;

                if (hold_body != nullptr) {
                    double tail_y = screen_hit_y - hit_height + note_size / 2;
                    double body_top = tail_y - manifest.hold_body_tail_offset * note_size;
                    double body_bottom = screen_hit_y + note_size / 2 - manifest.hold_body_head_offset * note_size;

                    if (stops_at_cut) {
                        body_bottom = std::min(body_bottom, cut_y + note_size / 2);
                    }

                    body_bottom = std::min(body_bottom, pos.y + size.y);

                    if (body_bottom > body_top) {
                        draw_hold_body(window, hold_body, hit_x, body_top, body_bottom, with_opacity(note_color(hit_x, screen_hit_y, window)));
                    }

                    olc::Decal* cap = sized_image(SkinImage::HOLD_CAP, column, key_count);

                    if (cap != nullptr && tail_y < body_bottom && cap->sprite->width > 0) {
                        double cap_height = note_size * cap->sprite->height / cap->sprite->width;
                        double cap_top = body_top - cap_height;
                        float top = (float)flip_y(body_top);
                        float end = (float)flip_y(cap_top);
                        float cap_texture_height = (float)cap->sprite->height;
                        float cap_inset = cap_texture_height > HOLD_EDGE_INSET ? HOLD_EDGE_INSET : 0.0f;
                        window->DrawPartialWarpedDecal(cap, {{(float)hit_x, top}, {(float)hit_x, end}, {(float)(hit_x + note_size), end}, {(float)(hit_x + note_size), top}}, {0.0f, cap_inset}, {(float)cap->sprite->width, cap_texture_height - cap_inset}, with_opacity(note_color(hit_x, screen_hit_y, window)));
                    }
                }
                else if (hit.hit_type == HitType::HOLD) {
                    for (double y_offset = 0; y_offset < hit_height; y_offset += (note_size / 4)) {
                        double hold_smear_y = screen_hit_y - hit_height + y_offset;
                        olc::Pixel hold_color = note_color(hit_x, hold_smear_y, window);
                        hold_color.a = manifest.hold_alpha;

                        if ((stops_at_cut && hold_smear_y > cut_y) || (hold_smear_y >= pos.y + size.y)) {
                            break;
                        }

                        draw_square(window, sized_image(SkinImage::NOTE, column, key_count), hit_x, hold_smear_y, with_opacity(hold_color));
                    }
                }

                if (hold_body != nullptr && holding) {
                    draw_square(window, sized_image(SkinImage::HOLD_HEAD, column, key_count), hit_x, std::min(screen_hit_y, jl_y), with_opacity(note_color(hit_x, jl_y, window)));
                }
                else if (((draw_notes_past_judge && !hit_played) || screen_hit_y <= jl_y) && screen_hit_y < pos.y + size.y) {
                    olc::Decal* head = sized_image(hit.hit_type == HitType::HOLD ? SkinImage::HOLD_HEAD : SkinImage::NOTE, column, key_count);
                    draw_square(window, head, hit_x, screen_hit_y, with_opacity(note_color(hit_x, screen_hit_y, window)));
                }
            }
        }

        const JudgedHit* jhit = attempt != nullptr && (draw_hit_score || draw_combo) ? attempt->get_last_scored() : nullptr;

        if (jhit != nullptr) {
            double t = (position - (jhit->hit_played ? (jhit->released ? jhit->attempt_hit.end_time : jhit->attempt_hit.time) : jhit->chart_hit.time + get_window_from_hit_acc_diff(HitScore::BAD, attempt->hit_accuracy))) / 1000;
            HitScore shown_score = jhit->released ? jhit->release_score : jhit->hit_score;
            olc::Decal* hs_skin = draw_hit_score ? skin->hit_score_decal(shown_score) : nullptr;
            float scale = smoothstep(0.8, 0.6, t * 4);

            if (hs_skin != nullptr) {
                olc::vf2d corner = {(float)(pos.x + size.x / 2 - hs_skin->sprite->width * scale / 2), (float)(overlay_y(judgement_height) - hs_skin->sprite->height * scale / 2)};
                olc::Pixel tint = skin->judgement_color(shown_score);
                tint.a = (uint8_t)smoothstep(255, 0, (t - 1) * 2);
                tint = with_opacity(tint);

                if (shown_score == HitScore::PERFECT) {
                    for (olc::vf2d offset : Skin::glow_offsets(std::max(1.0f, Ui::pxf(Skin::GRADE_GLOW_RADIUS) * scale))) {
                        window->DrawDecal(corner + offset, hs_skin, {scale, scale}, Skin::glow_color(tint, 1.0f));
                    }
                }

                window->DrawDecal(corner, hs_skin, {scale, scale}, tint);
            }

            if (draw_combo) {
                uint32_t combo = attempt->get_combo();

                if (!combo_decal || combo != last_rendered_combo) {
                    combo_decal = render_text_decal(skin->display_font_large, std::format("{}", combo), olc::WHITE);
                    last_rendered_combo = combo;
                }

                if (combo_decal) {
                    scale = smoothstep(0.6, 0.5, t * 6);

                    window->DrawDecal(
                        {(float)(pos.x + size.x / 2 - combo_decal.get_sprite()->Size().x * scale / 2),
                         (float)(overlay_y(combo_height) - combo_decal.get_sprite()->Size().y * scale / 2)},
                        combo_decal.get(),
                        {scale, scale},
                        with_opacity(skin->color(SkinColor::COMBO))
                    );
                }
            }
        }

        if (debug) {
            olc::Font& font = skin->display_font_small;
            olc::FontRect metrics = text_bounds(font, "Hgjy");
            float line = (float)metrics.size.y + Ui::pxf(4);
            olc::vf2d corner = {(float)pos.x + Ui::pxf(20), (float)pos.y + Ui::pxf(20) - metrics.offset.y};
            draw_text(font, std::format("POS {:.1f}", position), corner, skin->color(SkinColor::TEXT), 1.0f);
            draw_text(font, std::format("MB {}", chart->meter_beat_at(position)), corner + olc::vf2d{0.0f, line}, skin->color(SkinColor::TEXT), 1.0f);
            draw_text(font, std::format("SB {}", chart->section_beat_at(position)), corner + olc::vf2d{0.0f, line * 2}, skin->color(SkinColor::TEXT), 1.0f);
        }
    };

    olc::Pixel EntityField::note_color(double x, double y, olc::PixelGameEngine* window) const {
        if (skin->manifest().tint == SkinTint::NONE || !note_gradient) {
            return olc::WHITE;
        }

        return skin->title_gradient_at(x / window->GetScreenSize().x, flip_y(y) / window->GetScreenSize().y);
    }

    olc::Decal* EntityField::sized_image(SkinImage image, uint8_t column, uint8_t key_count) const {
        const MipDecal* decal = skin->image(image, column, key_count);

        if (decal == nullptr || decal->size().x <= 0) {
            return nullptr;
        }

        return decal->pick((float)(note_size / decal->size().x)).decal;
    }

    void EntityField::draw_square(olc::PixelGameEngine* window, olc::Decal* decal, double x, double y, olc::Pixel tint) const {
        if (decal == nullptr) {
            return;
        }

        double top = upscroll ? flip_y(y + note_size) : y;
        window->DrawWarpedDecal(decal, {{(float)x, (float)top}, {(float)x, (float)(top + note_size)}, {(float)(x + note_size), (float)(top + note_size)}, {(float)(x + note_size), (float)top}}, tint);
    }

    void EntityField::draw_hold_body(olc::PixelGameEngine* window, olc::Decal* body, double x, double tail, double head, olc::Pixel tint) const {
        float head_y = (float)flip_y(head);
        float tail_y = (float)flip_y(tail);
        float left = (float)x;
        float right = (float)(x + note_size);
        float width = (float)body->sprite->width;
        float height = (float)body->sprite->height;
        std::array<olc::vf2d, 4> corners = {{{left, head_y}, {left, tail_y}, {right, tail_y}, {right, head_y}}};
        const SkinManifest& manifest = skin->manifest();

        if (manifest.hold_style == HoldStyle::TILE && width > HOLD_EDGE_INSET * 2) {
            double aspect = manifest.hold_body_aspect.value_or(height / width);
            double period = std::max(1.0, note_size * aspect);
            float length = (float)((head - tail) / period * height);
            window->DrawPartialWarpedDecal(body, corners, {HOLD_EDGE_INSET, -length}, {width - HOLD_EDGE_INSET * 2, length}, tint);
            return;
        }

        float inset = height > HOLD_EDGE_INSET * 2 ? HOLD_EDGE_INSET : 0.0f;
        window->DrawPartialWarpedDecal(body, corners, {0.0f, inset}, {width, height - inset * 2}, tint);
    }

    olc::Pixel EntityField::with_opacity(olc::Pixel color) const {
        color.a = (uint8_t)std::lround(color.a * std::clamp(opacity, 0.0f, 1.0f));
        return color;
    }

    double EntityField::get_judge_bottom() const {
        return pos.y + size.y - judge_height;
    }

    double EntityField::flip_y(double y) const {
        return upscroll ? pos.y * 2 + size.y - y : y;
    }

    double EntityField::overlay_y(double height) const {
        return pos.y + size.y * (1 - height);
    }

    void EntityField::set_position(double position) {
        this->position = position;
    }

    void EntityField::update_longest_hold() {
        if (longest_hold_pitch == chart->pitch) {
            return;
        }

        longest_hold_pitch = chart->pitch;
        longest_hold_ms = 0;

        for (const Hit& hit : chart->hits) {
            if (hit.hit_type == HitType::HOLD) {
                longest_hold_ms = std::max(longest_hold_ms, hit.end_time - hit.time);
            }
        }
    }

    void EntityField::set_attempt(const Attempt* attempt) {
        this->attempt = attempt;
    }

    void EntityField::set_keys(uint32_t keys) {
        active_keys = keys;
    }

    void EntityField::set_key(uint8_t key) {
        active_keys |= (1u << key);
    }

    void EntityField::clear_keys() {
        active_keys = 0;
    }

    bool EntityField::get_key(uint8_t key) {
        return active_keys & (1u << key);
    }

    uint8_t EntityField::display_column(uint8_t key) const {
        return mirror ? (uint8_t)(chart->info.key_count - 1 - key) : key;
    }

    uint8_t EntityField::get_key_count() {
        return chart->info.key_count;
    }
}
