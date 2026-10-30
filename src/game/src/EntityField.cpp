#include "EntityField.h"
#include "Attempt.h"
#include "Chart.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "SkinSettings.h"
#include "gse/render/MipDecal.h"

#include "olcPixelGameEngine.h"
#include "gse/anim/Easing.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace nascent {
    static constexpr double SNAP_TOLERANCE_MS = 3.0;
    static constexpr double LIGHT_FADE_IN_MS = 80.0;
    static constexpr double LIGHT_FADE_OUT_MS = 120.0;
    static constexpr double LIGHT_SEARCH_MS = 1000.0;
    static constexpr double COMBO_BREAK_POP_MS = 200.0;
    static constexpr double KEY_LABEL_FADE_MS = 500.0;

    EntityField::EntityField(const Chart* chart, Skin* skin, olc::vd2d pos, olc::vd2d size) {
        this->chart = chart;
        this->skin = skin;
        this->pos = pos;
        this->size = size;

        scroll_speed = SettingsManager::settings.field_scroll_speed;
        set_lane_gap(std::nullopt);
        active_keys = 0;
    };

    std::array<olc::vd2d, 2> EntityField::gameplay_bounds(const Skin& skin, uint8_t key_count, olc::vd2d origin, olc::vd2d area) {
        double keys = std::max<uint8_t>(key_count, 1);
        double lane_width = std::min(area.y * skin_setting(skin, skin_settings::LANE_WIDTH) / 100.0, area.x / keys);
        double field_width = lane_width * keys;
        return {olc::vd2d{origin.x + (area.x - field_width) / 2, origin.y}, olc::vd2d{field_width, area.y}};
    }

    void EntityField::apply_gameplay_settings(double area_height) {
        draw_judge_colors = true;
        judge_alpha = 64;
        judge_height = area_height * skin_setting(*skin, skin_settings::JUDGEMENT_LINE_HEIGHT) / 100.0;
        upscroll = skin_toggle(*skin, skin_toggles::UPSCROLL);
        note_gradient = skin_toggle(*skin, skin_toggles::NOTE_GRADIENT);
        draw_judge = skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT_LINE);
        set_lane_gap(lane_gap_setting());
        draw_hit_score = skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT);
        draw_combo = skin_toggle(*skin, skin_toggles::SHOW_COMBO);
        lane_cover = SettingsManager::settings.field_lane_cover / 100.0;
        judgement_height = skin_setting(*skin, skin_settings::JUDGEMENT_HEIGHT) / 100.0;
        combo_height = skin_setting(*skin, skin_settings::COMBO_HEIGHT) / 100.0;
        note_opacity = (float)(skin_setting(*skin, skin_settings::NOTE_OPACITY) / 100.0);
        receptor_opacity = (float)(skin_setting(*skin, skin_settings::RECEPTOR_OPACITY) / 100.0);
    }

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
        double scroll_rate_px_per_ms = ((double)scroll_speed * screen_height / 100.0 / 1000.0);
        note_height_scale = layout("playfield.note_height_scale");
        note_height_scale = note_height_scale > 0 ? note_height_scale : 1.0;
        flip_notes = upscroll && layout("playfield.upscroll_flip_notes") > 0;
        flip_receptors = upscroll && layout("playfield.upscroll_flip_receptors") > 0;
        holding_since.assign(key_count, -1.0);
        any_holding = false;

        if (attempt != nullptr) {
            uint32_t combo = attempt->get_combo();

            if (position < broken_at) {
                broken_at = -1e9;
            }

            if (combo < last_combo) {
                broken_combo = last_combo;
                broken_at = position;
            }

            last_combo = combo;
        }

        update_snaps();
        update_presses();
        draw_stage(window, jl_y);

        if (underlay) {
            underlay(window);
        }

        draw_lane_lights(window, jl_y);
        draw_judgement_line(window, jl_y);
        draw_barlines(window, jl_y, scroll_rate_px_per_ms);

        bool receptors_over_notes = cover_bottom || layout("playfield.receptors_over_notes") > 0;

        if (!receptors_over_notes) {
            draw_receptors(window, jl_y);
        }

        update_longest_hold();
        float hold_edge_inset = (float)layout("playfield.hold_edge_inset");
        double hold_repeat_spacing = note_size * layout("playfield.hold_repeat_spacing");
        bool gradient_notes = manifest.tint != SkinTint::NONE && note_gradient;
        bool snap_colors = this->snap_colors.value_or(skin_toggle(*skin, skin_toggles::SNAP_COLORS));
        double smear_visible_from = flip_y(upscroll ? screen_height : 0) - note_size;

        for (size_t i = first_drawn_hit(scroll_rate_px_per_ms); i < chart->hits.size(); i++) {
            const Hit& hit = chart->hits[i];
            const JudgedHit* judged = attempt != nullptr ? &attempt->judged_hits[i] : nullptr;
            bool hit_played = judged != nullptr && judged->hit_played;
            bool holding = hit_played && hit.hit_type == HitType::HOLD && !judged->released;
            bool dropped = hit_played && judged->released && judged->release_played && judged->release_err < 0 && judged->release_score == HitScore::MISS;
            bool missed = judged != nullptr && !hit_played && judged->hit_score == HitScore::MISS;
            std::optional<uint8_t> snap = i < snaps.size() ? std::optional<uint8_t>(snaps[i]) : std::nullopt;
            bool stops_at_cut = !draw_notes_past_judge || hit_played;
            double cut_y = dropped ? jl_y - (judged->attempt_hit.end_time - position) * scroll_rate_px_per_ms : (double)jl_y;

            int32_t hit_y = (hit.time - position) * scroll_rate_px_per_ms;
            uint8_t column = display_column(hit.key);
            double hit_x = pos.x + (column * (note_size + note_x_spacing));

            if (holding && column < holding_since.size()) {
                holding_since[column] = std::max(0.0, position - judged->attempt_hit.time);
                any_holding = true;
            }
            int32_t hit_height = hit.hit_type == HitType::HOLD ? (hit.end_time - hit.time) * scroll_rate_px_per_ms : 0;

            int32_t screen_hit_y = jl_y - hit_y;

            if ((screen_hit_y + note_size) < pos.y) {
                break;
            }

            if ((screen_hit_y - hit_height) < pos.y + size.y) {
                SkinImage head_image = hit.hit_type == HitType::HOLD ? SkinImage::HOLD_HEAD : SkinImage::NOTE;
                bool snap_tinted = snap_colors && snap && sized_image(head_image, column, key_count, snap) == sized_image(head_image, column, key_count);
                olc::Pixel snap_color = snap_tinted ? skin->color((SkinColor)((size_t)SkinColor::SNAP_4TH + *snap)) : olc::WHITE;
                auto base_color = [&](double x, double y) {
                    return snap_tinted ? snap_color : note_color(x, y, window);
                };

                bool textured_body = manifest.hold_style == HoldStyle::STRETCH || manifest.hold_style == HoldStyle::TILE;
                olc::Decal* hold_body = hit.hit_type == HitType::HOLD && textured_body ? sized_image(holding ? SkinImage::HOLD_BODY_HELD : SkinImage::HOLD_BODY, column, key_count) : nullptr;

                if (hold_body != nullptr) {
                    double tail_y = screen_hit_y - hit_height + note_size / 2;
                    double body_top = tail_y - manifest.hold_body_tail_offset * note_size;
                    double body_bottom = screen_hit_y + note_size / 2 - manifest.hold_body_head_offset * note_size;

                    if (stops_at_cut) {
                        body_bottom = std::min(body_bottom, cut_y + note_size / 2);
                    }

                    body_bottom = std::min(body_bottom, pos.y + size.y);

                    if (body_bottom > body_top) {
                        draw_hold_body(window, hold_body, hit_x, body_top, body_bottom, hold_tint(note_tint(base_color(hit_x, screen_hit_y)), dropped || missed));
                    }

                    olc::Decal* cap = sized_image(SkinImage::HOLD_CAP, column, key_count);

                    if (cap != nullptr && tail_y < body_bottom && cap->sprite->width > 0) {
                        double cap_height = note_size * note_height_scale * cap->sprite->height / cap->sprite->width;
                        double cap_top = body_top - cap_height;
                        float top = (float)flip_y(body_top);
                        float end = (float)flip_y(cap_top);
                        float cap_texture_height = (float)cap->sprite->height;
                        float cap_inset = cap_texture_height > hold_edge_inset ? hold_edge_inset : 0.0f;
                        window->DrawPartialWarpedDecal(cap, {{(float)hit_x, top}, {(float)hit_x, end}, {(float)(hit_x + note_size), end}, {(float)(hit_x + note_size), top}}, {0.0f, cap_inset}, {(float)cap->sprite->width, cap_texture_height - cap_inset}, hold_tint(note_tint(base_color(hit_x, screen_hit_y)), dropped || missed));
                    }
                }
                else if (hit.hit_type == HitType::HOLD && hold_repeat_spacing > 0) {
                    olc::Decal* smear = sized_image(SkinImage::NOTE, column, key_count, snap);
                    double smear_origin = screen_hit_y - hit_height;
                    auto smear_tint = [&](double y) {
                        olc::Pixel hold_color = base_color(hit_x, y);
                        hold_color.a = manifest.hold_alpha;
                        return hold_tint(note_tint(hold_color), dropped || missed);
                    };
                    olc::Pixel tint = smear_tint(smear_origin);

                    for (double step = std::max(0.0, std::floor((smear_visible_from - smear_origin) / hold_repeat_spacing)); step * hold_repeat_spacing < hit_height; step++) {
                        double hold_smear_y = smear_origin + step * hold_repeat_spacing;

                        if ((stops_at_cut && hold_smear_y > cut_y) || (hold_smear_y >= pos.y + size.y)) {
                            break;
                        }

                        draw_note(window, smear, hit_x, hold_smear_y, gradient_notes && !snap_tinted ? smear_tint(hold_smear_y) : tint);
                    }
                }

                if (hold_body != nullptr && holding) {
                    draw_note(window, sized_image(SkinImage::HOLD_HEAD_HELD, column, key_count, snap), hit_x, std::min(screen_hit_y, jl_y), note_tint(base_color(hit_x, jl_y)));
                }
                else if (((draw_notes_past_judge && !hit_played) || screen_hit_y <= jl_y) && screen_hit_y < pos.y + size.y) {
                    bool hold = hit.hit_type == HitType::HOLD;
                    olc::Decal* head = sized_image(hold ? SkinImage::HOLD_HEAD : SkinImage::NOTE, column, key_count, snap);
                    draw_note(window, head, hit_x, screen_hit_y, hold_tint(note_tint(base_color(hit_x, screen_hit_y)), hold && (dropped || missed)));
                }
            }
        }

        draw_lane_cover(window, jl_y);

        if (receptors_over_notes) {
            draw_receptors(window, jl_y);
        }

        draw_lighting(window, jl_y);
        draw_stage_bottom(window);
        draw_intro(window, jl_y);

        const JudgedHit* jhit = attempt != nullptr && (draw_hit_score || draw_combo) ? attempt->get_last_scored() : nullptr;

        if (jhit != nullptr) {
            double t = (position - (jhit->hit_played ? (jhit->released ? jhit->attempt_hit.end_time : jhit->attempt_hit.time) : jhit->chart_hit.time + get_window_from_hit_acc_diff(HitScore::BAD, attempt->hit_accuracy))) / 1000;

            if (draw_hit_score) {
                draw_judgement(window, *jhit, t);
            }

            if (draw_combo) {
                draw_combo_counter(window, t);
            }
        }

        if (debug) {
            olc::Font& font = skin->display_font_small;
            olc::FontRect metrics = text_bounds(font, "Hgjy");
            float margin = Ui::pxf(layout("playfield.debug_margin"));
            float line = (float)metrics.size.y + Ui::pxf(layout("playfield.debug_line_gap"));
            olc::vf2d corner = {(float)pos.x + margin, (float)pos.y + margin - metrics.offset.y};
            draw_text(font, std::format("POS {:.1f}", position), corner, skin->color(SkinColor::TEXT), 1.0f);
            draw_text(font, std::format("MB {}", chart->meter_beat_at(position)), corner + olc::vf2d{0.0f, line}, skin->color(SkinColor::TEXT), 1.0f);
            draw_text(font, std::format("SB {}", chart->section_beat_at(position)), corner + olc::vf2d{0.0f, line * 2}, skin->color(SkinColor::TEXT), 1.0f);
        }
    };

    void EntityField::draw_combo_counter(olc::PixelGameEngine* window, double t) {
        float base_scale = (float)layout("playfield.combo_scale");
        float scale = smoothstep(layout("playfield.combo_pop_from"), layout("playfield.combo_pop_to"), t * layout("playfield.combo_pop_speed")) * base_scale;
        olc::vf2d center = {(float)(pos.x + size.x / 2), (float)overlay_y(combo_height)};
        draw_combo_text(window, std::format("{}", attempt->get_combo()), center, scale, with_opacity(skin->color(any_holding ? SkinColor::COMBO_HOLDING : SkinColor::COMBO)));

        double popped = position - broken_at;

        if (layout("playfield.combo_break_pop") > 0 && broken_combo > 0 && popped >= 0 && popped < COMBO_BREAK_POP_MS) {
            double progress = popped / COMBO_BREAK_POP_MS;
            olc::Pixel tint = skin->color(SkinColor::COMBO_BREAK);
            tint.a = (uint8_t)std::lround(tint.a * 0.8 * (1.0 - progress));
            window->SetDecalMode(olc::DecalMode::ADDITIVE);
            draw_combo_text(window, std::format("{}", broken_combo), center, base_scale * (float)(1.0 + 3.0 * progress), with_opacity(tint));
            window->SetDecalMode(olc::DecalMode::NORMAL);
        }
    }

    void EntityField::draw_combo_text(olc::PixelGameEngine* window, const std::string& text, olc::vf2d center, float scale, olc::Pixel tint) {
        uint8_t key_count = chart->info.key_count;

        if (skin->frame_count(SkinImage::COMBO_DIGIT, key_count) >= 10) {
            std::vector<const MipDecal*> digits;

            for (char c : text) {
                const MipDecal* digit = c >= '0' && c <= '9' ? skin->image_frame(SkinImage::COMBO_DIGIT, key_count, (size_t)(c - '0')) : nullptr;

                if (digit != nullptr && digit->size().x > 0 && digit->size().y > 0) {
                    digits.push_back(digit);
                }
            }

            if (digits.empty()) {
                return;
            }

            double digit_height = layout("playfield.combo_digit_height");
            double height = (digit_height > 0 ? size.y * digit_height : digits.front()->size().y * Ui::scale()) * scale;
            double overlap = height * layout("playfield.combo_digit_overlap");
            double total = -overlap * (double)(digits.size() - 1);

            for (const MipDecal* digit : digits) {
                total += height * digit->size().x / digit->size().y;
            }

            double x = center.x - total / 2;
            double top = center.y - height / 2;

            for (const MipDecal* digit : digits) {
                float drawn = (float)(height / digit->size().y);
                MipDecal::Pick level = digit->pick(drawn);
                window->DrawDecal({(float)x, (float)top}, level.decal, {drawn / level.factor, drawn / level.factor}, tint);
                x += height * digit->size().x / digit->size().y - overlap;
            }

            return;
        }

        olc::FontRect bounds = text_bounds(skin->combo_font, text);

        if (bounds.size.x > 0 && bounds.size.y > 0) {
            olc::vf2d corner = {center.x - bounds.size.x * scale / 2, center.y - bounds.size.y * scale / 2};
            draw_text(skin->combo_font, text, corner - olc::vf2d{(float)bounds.offset.x, (float)bounds.offset.y} * scale, tint, scale, false);
        }
    }

    void EntityField::draw_judgement(olc::PixelGameEngine* window, const JudgedHit& judged, double t) {
        HitScore shown_score = judged.released ? judged.release_score : judged.hit_score;
        float scale = smoothstep(layout("playfield.judgement_pop_from"), layout("playfield.judgement_pop_to"), t * layout("playfield.judgement_pop_speed")) * (float)layout("playfield.judgement_scale");
        uint8_t alpha = (uint8_t)smoothstep(255, 0, (t - layout("playfield.judgement_fade_delay")) * layout("playfield.judgement_fade_speed"));
        olc::vf2d center = {(float)(pos.x + size.x / 2), (float)overlay_y(judgement_height)};

        if (const MipDecal* picture = skin->judgement_image(shown_score, chart->info.key_count, t); picture != nullptr && picture->size().x > 0) {
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
            for (olc::vf2d offset : Skin::glow_offsets(std::max(1.0f, Ui::pxf(layout("playfield.judgement_glow_radius")) * scale))) {
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

    double EntityField::layout(std::string_view key) const {
        return skin->layout(key, chart->info.key_count);
    }

    olc::Decal* EntityField::sized_image(SkinImage image, uint8_t column, uint8_t key_count, std::optional<uint8_t> snap, std::optional<double> since) const {
        const MipDecal* decal = skin->image(image, column, key_count, snap, since);

        if (decal == nullptr || decal->size().x <= 0) {
            return nullptr;
        }

        return decal->pick((float)(note_size / decal->size().x)).decal;
    }

    void EntityField::draw_square(olc::PixelGameEngine* window, olc::Decal* decal, double x, double y, olc::Pixel tint, bool flipped) const {
        draw_box(window, decal, x, y, note_size, note_size, tint, flipped);
    }

    void EntityField::draw_note(olc::PixelGameEngine* window, olc::Decal* decal, double x, double y, olc::Pixel tint) const {
        if (decal == nullptr) {
            return;
        }

        if (skin->manifest(chart->info.key_count).note_shape != NoteShape::IMAGE || decal->sprite == nullptr || decal->sprite->width <= 0) {
            draw_square(window, decal, x, y, tint, flip_notes);
            return;
        }

        double height = note_size * note_height_scale * decal->sprite->height / decal->sprite->width;
        draw_box(window, decal, x, y + (note_size - height) / 2, note_size, height, tint, flip_notes);
    }

    void EntityField::draw_box(olc::PixelGameEngine* window, olc::Decal* decal, double x, double top, double width, double height, olc::Pixel tint, bool flipped) const {
        if (decal == nullptr) {
            return;
        }

        float screen_top = (float)(upscroll ? flip_y(top + height) : top);
        float left = (float)x;
        float right = (float)(x + width);
        float bottom = screen_top + (float)height;

        if (flipped) {
            std::swap(screen_top, bottom);
        }

        draw_quad(window, decal, {{{left, screen_top}, {left, bottom}, {right, bottom}, {right, screen_top}}}, tint);
    }

    void EntityField::draw_quad(olc::PixelGameEngine* window, olc::Decal* decal, const std::array<olc::vf2d, 4>& corners, olc::Pixel tint) const {
        if (decal == nullptr || decal->sprite == nullptr) {
            return;
        }

        float width = (float)decal->sprite->width;
        float height = (float)decal->sprite->height;
        float inset_x = width > 2.0f ? 0.5f : 0.0f;
        float inset_y = height > 2.0f ? 0.5f : 0.0f;
        window->DrawPartialWarpedDecal(decal, corners, {inset_x, inset_y}, {width - inset_x * 2.0f, height - inset_y * 2.0f}, tint);
    }

    void EntityField::draw_hold_body(olc::PixelGameEngine* window, olc::Decal* body, double x, double tail, double head, olc::Pixel tint) const {
        float head_y = (float)flip_y(head);
        float tail_y = (float)flip_y(tail);
        float left = (float)x;
        float right = (float)(x + note_size);
        float width = (float)body->sprite->width;
        float height = (float)body->sprite->height;
        float edge_inset = (float)layout("playfield.hold_edge_inset");
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
            float glow_radius = std::max(1.0f, Ui::pxf(layout("playfield.receptor_glow_radius")));
            double receptor_height = size.y * layout("playfield.receptor_height");

            for (uint8_t column = 0; column < key_count; column++) {
                double jl_x = pos.x + (column * (note_size + note_x_spacing));

                uint8_t lane = display_column(column);
                bool held = get_key(lane);
                float light = held ? 1.0f : (lane < lane_light.size() ? std::clamp(lane_light[lane], 0.0f, 1.0f) : 0.0f);
                std::optional<double> pressed_for = held && lane < press_positions.size() ? std::optional<double>((position - press_positions[lane]) / 1000.0) : std::nullopt;
                olc::Decal* receptor = sized_image(held ? SkinImage::RECEPTOR_PRESSED : SkinImage::RECEPTOR, column, key_count, std::nullopt, pressed_for);
                olc::Pixel color = manifest.tint == SkinTint::GRADIENT && draw_judge_colors && note_gradient ? skin->title_gradient_at((double)(jl_x - pos.x) / size.x, (flip_y(jl_y) - pos.y) / size.y) : olc::Pixel(255, 255, 255);
                double idle = manifest.receptor_alpha.value_or(judge_alpha);
                double pressed = manifest.receptor_pressed_alpha;
                color.a = (uint8_t)std::lround(std::clamp((idle + (pressed - idle) * light) * std::clamp(receptor_opacity, 0.0f, 1.0f), 0.0, 255.0));

                bool column_style = manifest.receptor_style == ReceptorStyle::COLUMN && receptor != nullptr && receptor->sprite != nullptr && receptor->sprite->width > 0;
                double column_height = note_size;

                if (column_style) {
                    column_height = receptor_height > 0 ? receptor_height : note_size * receptor->sprite->height / receptor->sprite->width;
                }

                double column_top = pos.y + size.y - column_height;

                auto draw_receptor = [&](double x, double y, olc::Pixel tint) {
                    if (column_style) {
                        draw_box(window, receptor, x, column_top + (y - jl_y), note_size, column_height, tint, flip_receptors);
                    }
                    else {
                        draw_square(window, receptor, x, y, tint, flip_receptors);
                    }
                };

                if (!lane_light.empty() && light > 0.0f) {
                    olc::Pixel glow = Skin::glow_color(color, light);

                    for (olc::vf2d offset : Skin::glow_offsets(glow_radius)) {
                        draw_receptor(jl_x + offset.x, jl_y + offset.y, with_opacity(glow));
                    }
                }

                draw_receptor(jl_x, jl_y, with_opacity(color));
            }
        }
    }

    void EntityField::draw_lane_cover(olc::PixelGameEngine* window, double judge_y) const {
        if (lane_cover == 0) {
            return;
        }

        double covered = std::clamp(std::abs(lane_cover), 0.0, 1.0) * (judge_y - pos.y);
        double fade = std::min(covered, (double)Ui::pxf(layout("playfield.lane_cover_fade")));
        double solid_from = lane_cover > 0 ? pos.y : pos.y + size.y;
        double edge = lane_cover > 0 ? pos.y + covered : judge_y - covered;
        double solid_to = lane_cover > 0 ? edge - fade : edge + fade;

        if (const MipDecal* cover = skin->image(SkinImage::LANE_COVER, 0, chart->info.key_count); cover != nullptr && cover->size().x > 0) {
            double top = std::min(flip_y(solid_from), flip_y(edge));
            double bottom = std::max(flip_y(solid_from), flip_y(edge));
            draw_stretched(window, cover, std::floor(pos.x), top, std::ceil(pos.x + size.x) - std::floor(pos.x), bottom - top, with_opacity(olc::WHITE));
            return;
        }

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

    void EntityField::update_snaps() {
        if (snaps_pitch == chart->pitch && snaps.size() == chart->hits.size()) {
            return;
        }

        snaps_pitch = chart->pitch;
        snaps.assign(chart->hits.size(), (uint8_t)(NOTE_SNAP_COUNT - 1));

        if (chart->timing_points.empty()) {
            return;
        }

        for (size_t index = 0; index < chart->hits.size(); index++) {
            double time = chart->hits[index].time;
            auto after = std::upper_bound(chart->timing_points.begin(), chart->timing_points.end(), time + 1.0, [](double value, const ChartTimingPoint& point) {
                return value < point.time;
            });
            const ChartTimingPoint& point = after == chart->timing_points.begin() ? *after : *std::prev(after);

            if (!(point.beat_length > 0)) {
                continue;
            }

            double beats = (time - point.time) / point.beat_length;
            double fraction = beats - std::floor(beats);
            double tolerance = std::min(SNAP_TOLERANCE_MS, point.beat_length / NOTE_SNAP_DIVISIONS.back() / 2.0) / point.beat_length;

            for (size_t snap = 0; snap < NOTE_SNAP_COUNT; snap++) {
                double steps = fraction * NOTE_SNAP_DIVISIONS[snap];

                if (std::abs(steps - std::round(steps)) / NOTE_SNAP_DIVISIONS[snap] <= tolerance) {
                    snaps[index] = (uint8_t)snap;
                    break;
                }
            }
        }
    }

    void EntityField::update_presses() {
        uint8_t key_count = chart->info.key_count;

        if (pressed_lanes.size() != key_count) {
            pressed_lanes.assign(key_count, false);
            press_positions.assign(key_count, position);
            release_positions.assign(key_count, -1e9);
        }

        for (uint8_t lane = 0; lane < key_count; lane++) {
            bool held = get_key(lane);

            if (held && !pressed_lanes[lane]) {
                press_positions[lane] = position;
            }
            else if (!held && pressed_lanes[lane]) {
                release_positions[lane] = position;
            }

            if (position < press_positions[lane]) {
                press_positions[lane] = position;
            }

            pressed_lanes[lane] = held;
        }
    }

    float EntityField::lane_light_level(uint8_t lane) const {
        if (lane < lane_light.size()) {
            return std::clamp(lane_light[lane], 0.0f, 1.0f);
        }

        if (lane < pressed_lanes.size() && pressed_lanes[lane]) {
            return 1.0f;
        }

        double fade = layout("playfield.lane_light_fade");

        if (lane >= release_positions.size() || !(fade > 0)) {
            return 0.0f;
        }

        double since = (position - release_positions[lane]) / 1000.0;
        return since < 0 ? 0.0f : (float)std::clamp(1.0 - since / fade, 0.0, 1.0);
    }

    void EntityField::draw_stretched(olc::PixelGameEngine* window, const MipDecal* image, double x, double top, double width, double height, olc::Pixel tint) const {
        if (image == nullptr || image->size().x <= 0 || image->size().y <= 0 || width <= 0 || height <= 0) {
            return;
        }

        olc::Decal* decal = image->pick((float)(width / image->size().x)).decal;
        float left = (float)x;
        float right = (float)(x + width);
        float upper = (float)top;
        float lower = (float)(top + height);

        if (upscroll) {
            std::swap(upper, lower);
        }

        draw_quad(window, decal, {{{left, upper}, {left, lower}, {right, lower}, {right, upper}}}, tint);
    }

    void EntityField::draw_stage(olc::PixelGameEngine* window, int32_t) {
        const uint8_t key_count = chart->info.key_count;
        olc::Pixel tint = with_opacity(olc::WHITE);
        double top = pos.y;
        double height = size.y;

        for (uint8_t column = 0; column < key_count; column++) {
            double x = pos.x + column * (note_size + note_x_spacing);
            draw_stretched(window, skin->image(SkinImage::LANE_BACKGROUND, column, key_count), x, top, note_size, height, tint);
        }

        auto tall_width = [&](const MipDecal* image) {
            return image != nullptr && image->size().y > 0 ? height * image->size().x / image->size().y : 0.0;
        };

        for (uint8_t column = 0; column + 1 < key_count; column++) {
            const MipDecal* separator = skin->image(SkinImage::LANE_SEPARATOR, column, key_count);
            double width = tall_width(separator);
            double center = pos.x + (column + 1) * (note_size + note_x_spacing) - note_x_spacing / 2;
            draw_stretched(window, separator, center - width / 2, top, width, height, tint);
        }

        const MipDecal* left = skin->image(SkinImage::STAGE_LEFT, 0, key_count);
        const MipDecal* right = skin->image(SkinImage::STAGE_RIGHT, 0, key_count);
        draw_stretched(window, left, pos.x - tall_width(left), top, tall_width(left), height, tint);
        draw_stretched(window, right, pos.x + size.x, top, tall_width(right), height, tint);
    }

    void EntityField::draw_lane_lights(olc::PixelGameEngine* window, int32_t jl_y) {
        const uint8_t key_count = chart->info.key_count;
        double height = size.y * layout("playfield.lane_light_height");

        for (uint8_t column = 0; column < key_count; column++) {
            uint8_t lane = display_column(column);
            float level = lane_light_level(lane);
            const MipDecal* light = level > 0.0f ? skin->image(SkinImage::LANE_LIGHT, column, key_count) : nullptr;

            if (light == nullptr) {
                continue;
            }

            double x = pos.x + column * (note_size + note_x_spacing);
            double bottom = jl_y + note_size - size.y * layout("playfield.lane_light_offset");
            olc::Pixel tint = olc::WHITE;
            tint.a = (uint8_t)std::lround(255 * level);
            double drawn_top = upscroll ? flip_y(bottom) : bottom - height;
            draw_stretched(window, light, x, drawn_top, note_size, height, with_opacity(tint));
        }
    }

    void EntityField::draw_judgement_line(olc::PixelGameEngine* window, int32_t jl_y) {
        const MipDecal* line = skin->image(SkinImage::JUDGEMENT_LINE, 0, chart->info.key_count);

        if (line == nullptr || line->size().x <= 0) {
            return;
        }

        double image_height = size.y * layout("playfield.judgement_line_image_height");
        double height = image_height > 0 ? image_height : size.x * line->size().y / line->size().x;
        double center = flip_y(hit_line_y(jl_y));
        draw_stretched(window, line, pos.x, center - height / 2, size.x, height, with_opacity(olc::WHITE));
    }

    double EntityField::hit_line_y(int32_t jl_y) const {
        return jl_y + note_size / 2 - size.y * layout("playfield.hit_line_offset");
    }

    void EntityField::draw_barlines(olc::PixelGameEngine* window, int32_t jl_y, double rate_px_per_ms) {
        double height = size.y * layout("playfield.barline_height");

        if (!(height > 0) || !(rate_px_per_ms > 0) || chart->timing_points.empty()) {
            return;
        }

        double line_y = hit_line_y(jl_y);
        double latest = position + (line_y - pos.y) / rate_px_per_ms;
        double earliest = position - (pos.y + size.y - line_y + height) / rate_px_per_ms;
        olc::Pixel color = with_opacity(skin->color(SkinColor::BARLINE));
        const std::vector<ChartTimingPoint>& points = chart->timing_points;

        for (size_t index = 0; index < points.size(); index++) {
            const ChartTimingPoint& point = points[index];
            double measure = (double)point.beat_length * std::max<uint8_t>(1, point.meter);
            double end = index + 1 < points.size() ? (double)points[index + 1].time : latest + measure;

            if (!(measure > 0) || end <= earliest || point.time > latest) {
                continue;
            }

            for (double bar = std::max(0.0, std::ceil((earliest - point.time) / measure));; bar++) {
                double time = point.time + bar * measure;

                if (time >= end || time > latest) {
                    break;
                }

                double bottom = line_y - (time - position) * rate_px_per_ms;
                double top = upscroll ? flip_y(bottom) : bottom - height;
                window->FillRectDecal({(float)pos.x, (float)top}, {(float)size.x, (float)height}, color);
            }
        }
    }

    void EntityField::draw_light(olc::PixelGameEngine* window, const MipDecal* image, double center_x, double center_y, double height, float alpha) const {
        if (image == nullptr || image->size().x <= 0 || image->size().y <= 0 || alpha <= 0) {
            return;
        }

        double drawn_height = height > 0 ? height : note_size * image->size().y / image->size().x;
        double width = drawn_height * image->size().x / image->size().y;
        olc::Decal* decal = image->pick((float)(width / image->size().x)).decal;
        olc::Pixel tint = olc::WHITE;
        tint.a = (uint8_t)std::lround(255.0f * std::clamp(alpha, 0.0f, 1.0f));
        float left = (float)(center_x - width / 2);
        float right = (float)(center_x + width / 2);
        float top = (float)(center_y - drawn_height / 2);
        float bottom = (float)(center_y + drawn_height / 2);
        draw_quad(window, decal, {{{left, top}, {left, bottom}, {right, bottom}, {right, top}}}, with_opacity(tint));
    }

    void EntityField::draw_lighting(olc::PixelGameEngine* window, int32_t jl_y) {
        const uint8_t key_count = chart->info.key_count;
        std::vector<double> hit_since(key_count, -1.0);
        std::vector<double> release_since(key_count, -1.0);

        if (attempt != nullptr && attempt->judged_hits.size() == chart->hits.size()) {
            auto start = std::lower_bound(latest_ends.begin(), latest_ends.end(), position - LIGHT_FADE_IN_MS - LIGHT_FADE_OUT_MS - LIGHT_SEARCH_MS, [](int32_t end, double time) {
                return end < time;
            });

            for (size_t index = (size_t)(start - latest_ends.begin()); index < chart->hits.size() && chart->hits[index].time <= position + LIGHT_SEARCH_MS; index++) {
                const Hit& hit = chart->hits[index];
                const JudgedHit& judged = attempt->judged_hits[index];
                uint8_t column = display_column(hit.key);

                if (!judged.hit_played || column >= key_count) {
                    continue;
                }

                auto lit_at = [&](double time) {
                    double since = position - time;

                    if (since >= 0 && since <= LIGHT_FADE_IN_MS + LIGHT_FADE_OUT_MS && (hit_since[column] < 0 || since < hit_since[column])) {
                        hit_since[column] = since;
                    }
                };

                if (judged.hit_score != HitScore::MISS) {
                    lit_at(judged.attempt_hit.time);
                }

                if (hit.hit_type == HitType::HOLD && judged.released) {
                    if (judged.release_played && judged.release_score != HitScore::MISS) {
                        lit_at(judged.attempt_hit.end_time);
                    }

                    double since = position - judged.attempt_hit.end_time;

                    if (since >= 0 && since <= LIGHT_FADE_OUT_MS && (release_since[column] < 0 || since < release_since[column])) {
                        release_since[column] = since;
                    }
                }
            }
        }

        double center_y = flip_y(hit_line_y(jl_y));
        double hit_height = size.y * layout("playfield.hit_light_height");
        double hold_height = size.y * layout("playfield.hold_light_height");
        window->SetDecalMode(olc::DecalMode::ADDITIVE);

        for (uint8_t column = 0; column < key_count; column++) {
            double center_x = pos.x + column * (note_size + note_x_spacing) + note_size / 2;

            if (hit_since[column] >= 0) {
                double since = hit_since[column];
                float alpha = (float)(since < LIGHT_FADE_IN_MS ? since / LIGHT_FADE_IN_MS : 1.0 - (since - LIGHT_FADE_IN_MS) / LIGHT_FADE_OUT_MS);
                draw_light(window, skin->image(SkinImage::HIT_LIGHT, column, key_count, std::nullopt, since / 1000.0), center_x, center_y, hit_height, alpha);
            }

            float hold_alpha = 0.0f;

            if (column < holding_since.size() && holding_since[column] >= 0) {
                hold_alpha = (float)std::min(1.0, holding_since[column] / LIGHT_FADE_IN_MS);
            }
            else if (release_since[column] >= 0) {
                hold_alpha = (float)(1.0 - release_since[column] / LIGHT_FADE_OUT_MS);
            }

            if (hold_alpha > 0) {
                draw_light(window, skin->image(SkinImage::HOLD_LIGHT, column, key_count), center_x, center_y, hold_height, hold_alpha);
            }
        }

        window->SetDecalMode(olc::DecalMode::NORMAL);
    }

    void EntityField::draw_stage_bottom(olc::PixelGameEngine* window) {
        const MipDecal* image = skin->image(SkinImage::STAGE_BOTTOM, 0, chart->info.key_count);

        if (image == nullptr || image->size().x <= 0 || image->size().y <= 0) {
            return;
        }

        double height = size.y * layout("playfield.stage_bottom_height");
        double width = height > 0 ? height * image->size().x / image->size().y : size.x;
        height = height > 0 ? height : width * image->size().y / image->size().x;
        float left = (float)(pos.x + size.x / 2 - width / 2);
        float right = (float)(left + width);
        float top = (float)(upscroll ? pos.y : pos.y + size.y - height);
        float bottom = (float)(top + height);
        olc::Decal* decal = image->pick((float)(width / image->size().x)).decal;
        draw_quad(window, decal, {{{left, top}, {left, bottom}, {right, bottom}, {right, top}}}, with_opacity(olc::WHITE));
    }

    void EntityField::draw_intro(olc::PixelGameEngine* window, int32_t jl_y) {
        if (!show_intro || chart->hits.empty() || position >= chart->hits.front().time) {
            return;
        }

        const uint8_t key_count = chart->info.key_count;
        double first = chart->hits.front().time;
        const ChartTimingPoint* point = chart->timing_point_at(first);
        double beat = point != nullptr && point->beat_length > 0 ? point->beat_length : 500.0;
        double bar = beat * (point != nullptr ? std::max<uint8_t>(1, point->meter) : 4);
        double line_y = hit_line_y(jl_y);
        double until_first = first - position;

        if (until_first <= bar * 3 && first >= bar * 3 && std::fmod(until_first / beat, 1.0) < 0.5) {
            double arrow_height = size.y * layout("playfield.warning_arrow_height");

            for (uint8_t column = 0; column < key_count; column++) {
                olc::Decal* arrow = sized_image(SkinImage::WARNING_ARROW, column, key_count);

                if (arrow == nullptr || arrow->sprite == nullptr || arrow->sprite->width <= 0) {
                    continue;
                }

                double height = arrow_height > 0 ? arrow_height : note_size * arrow->sprite->height / arrow->sprite->width;
                double width = arrow_height > 0 ? height * arrow->sprite->width / arrow->sprite->height : note_size;
                double x = pos.x + column * (note_size + note_x_spacing) + (note_size - width) / 2;
                draw_box(window, arrow, x, line_y - height, width, height, with_opacity(olc::WHITE), upscroll);
            }
        }

        if (layout("playfield.key_labels") > 0 && key_labels.size() >= key_count) {
            olc::Font& font = skin->display_font_small;
            olc::Pixel color = skin->color(SkinColor::KEY_LABEL);
            color.a = (uint8_t)std::lround(color.a * std::clamp(until_first / KEY_LABEL_FADE_MS, 0.0, 1.0));

            for (uint8_t column = 0; column < key_count; column++) {
                const std::string& label = key_labels[column];
                olc::FontRect bounds = text_bounds(font, label);

                if (bounds.size.x <= 0 || bounds.size.y <= 0) {
                    continue;
                }

                double center_x = pos.x + column * (note_size + note_x_spacing) + note_size / 2;
                double center_y = flip_y(line_y + note_size / 2);
                olc::vf2d corner = {(float)(center_x - bounds.size.x / 2.0), (float)(center_y - bounds.size.y / 2.0)};
                draw_text(font, label, corner - olc::vf2d{(float)bounds.offset.x, (float)bounds.offset.y}, with_opacity(color), 1.0f);
            }
        }
    }

    olc::Pixel EntityField::hold_tint(olc::Pixel color, bool dropped) const {
        if (!dropped) {
            return color;
        }

        olc::Pixel shade = skin->color(SkinColor::HOLD_DROPPED);
        return olc::Pixel((uint8_t)(color.r * shade.r / 255), (uint8_t)(color.g * shade.g / 255), (uint8_t)(color.b * shade.b / 255), (uint8_t)(color.a * shade.a / 255));
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
        if (longest_hold_pitch == chart->pitch && latest_ends.size() == chart->hits.size()) {
            return;
        }

        longest_hold_pitch = chart->pitch;
        longest_hold_ms = 0;
        latest_ends.clear();
        latest_ends.reserve(chart->hits.size());

        for (const Hit& hit : chart->hits) {
            int32_t end = hit.time;

            if (hit.hit_type == HitType::HOLD) {
                longest_hold_ms = std::max(longest_hold_ms, hit.end_time - hit.time);
                end = std::max(end, hit.end_time);
            }

            latest_ends.push_back(latest_ends.empty() ? end : std::max(latest_ends.back(), end));
        }
    }

    size_t EntityField::first_drawn_hit(double rate_px_per_ms) const {
        double field_ms = rate_px_per_ms > 0 ? size.y / rate_px_per_ms : 0;
        double earliest_time = position - longest_hold_ms - field_ms;
        auto first_hit = std::lower_bound(chart->hits.begin(), chart->hits.end(), earliest_time, [](const Hit& hit, double time) {
            return hit.time < time;
        });
        size_t first = (size_t)(first_hit - chart->hits.begin());

        if (!(rate_px_per_ms > 0)) {
            return first;
        }

        double earliest_end = position - field_ms - (note_size + judge_height + 2) / rate_px_per_ms;
        auto first_end = std::lower_bound(latest_ends.begin(), latest_ends.end(), earliest_end, [](int32_t end, double time) {
            return end < time;
        });
        return std::max(first, (size_t)(first_end - latest_ends.begin()));
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
