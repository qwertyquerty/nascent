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

        double spacing = skin->manifest(chart->info.key_count).key_spacing;
        note_size = size.x / (keys + keys * spacing - spacing);
        note_x_spacing = note_size * spacing;
    }

    void EntityField::draw(olc::PixelGameEngine* window) {
        int32_t screen_height = window->ScreenHeight();
        const uint8_t key_count = chart->info.key_count;
        const SkinManifest& manifest = skin->manifest(key_count);

        int32_t jl_y = pos.y + size.y - note_size - judge_height;
        bool cover_bottom = lane_cover < 0;

        if (!cover_bottom) {
            draw_receptors(window, jl_y);
        }

        double scroll_rate_px_per_ms = ((double)scroll_speed * screen_height / 100.0 / 1000.0);

        update_longest_hold();
        float hold_edge_inset = (float)skin->layout("playfield.hold_edge_inset");
        double hold_repeat_spacing = note_size * skin->layout("playfield.hold_repeat_spacing");
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
                        draw_hold_body(window, hold_body, hit_x, body_top, body_bottom, note_tint(note_color(hit_x, screen_hit_y, window)));
                    }

                    olc::Decal* cap = sized_image(SkinImage::HOLD_CAP, column, key_count);

                    if (cap != nullptr && tail_y < body_bottom && cap->sprite->width > 0) {
                        double cap_height = note_size * cap->sprite->height / cap->sprite->width;
                        double cap_top = body_top - cap_height;
                        float top = (float)flip_y(body_top);
                        float end = (float)flip_y(cap_top);
                        float cap_texture_height = (float)cap->sprite->height;
                        float cap_inset = cap_texture_height > hold_edge_inset ? hold_edge_inset : 0.0f;
                        window->DrawPartialWarpedDecal(cap, {{(float)hit_x, top}, {(float)hit_x, end}, {(float)(hit_x + note_size), end}, {(float)(hit_x + note_size), top}}, {0.0f, cap_inset}, {(float)cap->sprite->width, cap_texture_height - cap_inset}, note_tint(note_color(hit_x, screen_hit_y, window)));
                    }
                }
                else if (hit.hit_type == HitType::HOLD) {
                    for (double y_offset = 0; hold_repeat_spacing > 0 && y_offset < hit_height; y_offset += hold_repeat_spacing) {
                        double hold_smear_y = screen_hit_y - hit_height + y_offset;
                        olc::Pixel hold_color = note_color(hit_x, hold_smear_y, window);
                        hold_color.a = manifest.hold_alpha;

                        if ((stops_at_cut && hold_smear_y > cut_y) || (hold_smear_y >= pos.y + size.y)) {
                            break;
                        }

                        draw_square(window, sized_image(SkinImage::NOTE, column, key_count), hit_x, hold_smear_y, note_tint(hold_color));
                    }
                }

                if (hold_body != nullptr && holding) {
                    draw_square(window, sized_image(SkinImage::HOLD_HEAD, column, key_count), hit_x, std::min(screen_hit_y, jl_y), note_tint(note_color(hit_x, jl_y, window)));
                }
                else if (((draw_notes_past_judge && !hit_played) || screen_hit_y <= jl_y) && screen_hit_y < pos.y + size.y) {
                    olc::Decal* head = sized_image(hit.hit_type == HitType::HOLD ? SkinImage::HOLD_HEAD : SkinImage::NOTE, column, key_count);
                    draw_square(window, head, hit_x, screen_hit_y, note_tint(note_color(hit_x, screen_hit_y, window)));
                }
            }
        }

        draw_lane_cover(window, jl_y);

        if (cover_bottom) {
            draw_receptors(window, jl_y);
        }

        const JudgedHit* jhit = attempt != nullptr && (draw_hit_score || draw_combo) ? attempt->get_last_scored() : nullptr;

        if (jhit != nullptr) {
            double t = (position - (jhit->hit_played ? (jhit->released ? jhit->attempt_hit.end_time : jhit->attempt_hit.time) : jhit->chart_hit.time + get_window_from_hit_acc_diff(HitScore::BAD, attempt->hit_accuracy))) / 1000;

            if (draw_hit_score) {
                draw_judgement(window, *jhit, t);
            }

            if (draw_combo) {
                uint32_t combo = attempt->get_combo();

                if (!combo_decal || combo != last_rendered_combo) {
                    combo_decal = render_text_decal(skin->combo_font, std::format("{}", combo), olc::WHITE);
                    last_rendered_combo = combo;
                }

                if (combo_decal) {
                    float scale = smoothstep(skin->layout("playfield.combo_pop_from"), skin->layout("playfield.combo_pop_to"), t * skin->layout("playfield.combo_pop_speed")) * (float)skin->layout("playfield.combo_scale");

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
            float margin = Ui::pxf(skin->layout("playfield.debug_margin"));
            float line = (float)metrics.size.y + Ui::pxf(skin->layout("playfield.debug_line_gap"));
            olc::vf2d corner = {(float)pos.x + margin, (float)pos.y + margin - metrics.offset.y};
            draw_text(font, std::format("POS {:.1f}", position), corner, skin->color(SkinColor::TEXT), 1.0f);
            draw_text(font, std::format("MB {}", chart->meter_beat_at(position)), corner + olc::vf2d{0.0f, line}, skin->color(SkinColor::TEXT), 1.0f);
            draw_text(font, std::format("SB {}", chart->section_beat_at(position)), corner + olc::vf2d{0.0f, line * 2}, skin->color(SkinColor::TEXT), 1.0f);
        }
    };

    void EntityField::draw_judgement(olc::PixelGameEngine* window, const JudgedHit& judged, double t) {
        HitScore shown_score = judged.released ? judged.release_score : judged.hit_score;
        float scale = smoothstep(skin->layout("playfield.judgement_pop_from"), skin->layout("playfield.judgement_pop_to"), t * skin->layout("playfield.judgement_pop_speed")) * (float)skin->layout("playfield.judgement_scale");
        uint8_t alpha = (uint8_t)smoothstep(255, 0, (t - skin->layout("playfield.judgement_fade_delay")) * skin->layout("playfield.judgement_fade_speed"));
        olc::vf2d center = {(float)(pos.x + size.x / 2), (float)overlay_y(judgement_height)};

        if (const MipDecal* picture = skin->judgement_image(shown_score, chart->info.key_count); picture != nullptr && picture->size().x > 0) {
            float drawn = scale * (float)Ui::scale();
            MipDecal::Pick level = picture->pick(drawn);
            olc::Pixel tint = olc::WHITE;
            tint.a = alpha;
            window->DrawDecal(center - picture->size() * drawn / 2.0f, level.decal, {drawn / level.factor, drawn / level.factor}, with_opacity(tint));
            return;
        }

        olc::Decal* text = skin->hit_score_decal(shown_score);

        if (text == nullptr) {
            return;
        }

        olc::vf2d corner = center - olc::vf2d{(float)text->sprite->width, (float)text->sprite->height} * scale / 2.0f;
        olc::Pixel tint = skin->judgement_color(shown_score);
        tint.a = alpha;
        tint = with_opacity(tint);

        if (shown_score == HitScore::PERFECT) {
            for (olc::vf2d offset : Skin::glow_offsets(std::max(1.0f, Ui::pxf(skin->layout("playfield.judgement_glow_radius")) * scale))) {
                window->DrawDecal(corner + offset, text, {scale, scale}, Skin::glow_color(tint, 1.0f));
            }
        }

        window->DrawDecal(corner, text, {scale, scale}, tint);
    }

    olc::Pixel EntityField::note_color(double x, double y, olc::PixelGameEngine* window) const {
        if (skin->manifest(chart->info.key_count).tint == SkinTint::NONE || !note_gradient) {
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
        float edge_inset = (float)skin->layout("playfield.hold_edge_inset");
        std::array<olc::vf2d, 4> corners = {{{left, head_y}, {left, tail_y}, {right, tail_y}, {right, head_y}}};
        const SkinManifest& manifest = skin->manifest(chart->info.key_count);

        if (manifest.hold_style == HoldStyle::TILE && width > edge_inset * 2) {
            double aspect = manifest.hold_body_aspect.value_or(height / width);
            double period = std::max(1.0, note_size * aspect);
            float length = (float)((head - tail) / period * height);
            window->DrawPartialWarpedDecal(body, corners, {edge_inset, -length}, {width - edge_inset * 2, length}, tint);
            return;
        }

        float inset = height > edge_inset * 2 ? edge_inset : 0.0f;
        window->DrawPartialWarpedDecal(body, corners, {0.0f, inset}, {width, height - inset * 2}, tint);
    }

    olc::Pixel EntityField::with_opacity(olc::Pixel color) const {
        color.a = (uint8_t)std::lround(color.a * std::clamp(opacity, 0.0f, 1.0f));
        return color;
    }

    olc::Pixel EntityField::note_tint(olc::Pixel color) const {
        float factor = skin->manifest(chart->info.key_count).note_alpha / 255.0f * std::clamp(note_opacity, 0.0f, 1.0f);
        color.a = (uint8_t)std::lround(color.a * factor);
        return with_opacity(color);
    }

    double EntityField::get_judge_bottom() const {
        return pos.y + size.y - judge_height;
    }

    void EntityField::draw_receptors(olc::PixelGameEngine* window, int32_t jl_y) {
        const uint8_t key_count = chart->info.key_count;
        const SkinManifest& manifest = skin->manifest(key_count);

        if (draw_judge) {
            float glow_radius = std::max(1.0f, Ui::pxf(skin->layout("playfield.receptor_glow_radius")));

            for (uint8_t column = 0; column < key_count; column++) {
                int32_t jl_x = pos.x + (column * (note_size + note_x_spacing));

                uint8_t lane = display_column(column);
                bool held = get_key(lane);
                float light = held ? 1.0f : (lane < lane_light.size() ? std::clamp(lane_light[lane], 0.0f, 1.0f) : 0.0f);
                olc::Decal* receptor = sized_image(held ? SkinImage::RECEPTOR_PRESSED : SkinImage::RECEPTOR, column, key_count);
                olc::Pixel color = manifest.tint == SkinTint::GRADIENT && draw_judge_colors && note_gradient ? skin->title_gradient_at((double)(jl_x - pos.x) / size.x, (flip_y(jl_y) - pos.y) / size.y) : olc::Pixel(255, 255, 255);
                double idle = manifest.receptor_alpha.value_or(judge_alpha);
                double pressed = manifest.receptor_pressed_alpha;
                color.a = (uint8_t)std::lround(std::clamp((idle + (pressed - idle) * light) * std::clamp(receptor_opacity, 0.0f, 1.0f), 0.0, 255.0));

                if (!lane_light.empty() && light > 0.0f) {
                    olc::Pixel glow = Skin::glow_color(color, light);

                    for (olc::vf2d offset : Skin::glow_offsets(glow_radius)) {
                        draw_square(window, receptor, jl_x + offset.x, jl_y + offset.y, with_opacity(glow));
                    }
                }

                draw_square(window, receptor, jl_x, jl_y, with_opacity(color));
            }
        }
    }

    void EntityField::draw_lane_cover(olc::PixelGameEngine* window, double judge_y) const {
        if (lane_cover == 0) {
            return;
        }

        double covered = std::clamp(std::abs(lane_cover), 0.0, 1.0) * (judge_y - pos.y);
        double fade = std::min(covered, (double)Ui::pxf(skin->layout("playfield.lane_cover_fade")));
        double solid_from = lane_cover > 0 ? pos.y : pos.y + size.y;
        double edge = lane_cover > 0 ? pos.y + covered : judge_y - covered;
        double solid_to = lane_cover > 0 ? edge - fade : edge + fade;

        olc::Pixel opaque = skin->color(SkinColor::SCREEN_BACKGROUND);
        opaque.a = 255;
        opaque = with_opacity(opaque);
        olc::Pixel clear = opaque;
        clear.a = 0;

        float left = (float)std::floor(pos.x);
        float width = (float)std::ceil(pos.x + size.x) - left;
        float solid_top = (float)std::min(flip_y(solid_from), flip_y(solid_to));
        float solid_bottom = (float)std::max(flip_y(solid_from), flip_y(solid_to));

        if (solid_bottom > solid_top) {
            window->FillRectDecal({left, solid_top}, {width, solid_bottom - solid_top}, opaque);
        }

        float opaque_y = (float)flip_y(solid_to);
        float clear_y = (float)flip_y(edge);
        olc::Pixel top_color = opaque_y < clear_y ? opaque : clear;
        olc::Pixel bottom_color = opaque_y < clear_y ? clear : opaque;
        window->GradientFillRectDecal({left, std::min(opaque_y, clear_y)}, {width, std::abs(clear_y - opaque_y)}, top_color, bottom_color, bottom_color, top_color);
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
