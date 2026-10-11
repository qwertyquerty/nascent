#include "EntityField.h"
#include "Attempt.h"
#include "Chart.h"
#include "Entity.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "Util.h"

#include "olcPixelGameEngine.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace nascent {
    EntityField::EntityField(const Chart* chart, Skin* skin, olc::vd2d pos, olc::vd2d size) {
        this->chart = chart;
        this->skin = skin;
        this->pos = pos;
        this->size = size;

        scroll_speed = SettingsManager::settings.field_scroll_speed;

        note_size = (size.x) / (chart->info.key_count + chart->info.key_count * skin->field_key_spacing_ratio - skin->field_key_spacing_ratio);
        note_x_spacing = note_size * skin->field_key_spacing_ratio;

        active_keys = 0;
    };

    void EntityField::init(Scene*) {

    };

    void EntityField::update(Scene*, float) {

    };

    void EntityField::draw(olc::PixelGameEngine* window) {
        int32_t screen_height = window->ScreenHeight();
        const uint8_t key_count = chart->info.key_count;

        // Draw judge line
        int32_t jl_y = pos.y + size.y - note_size - judge_height;

        if (draw_judge) {
            for (uint8_t key = 0; key < key_count; key++) {
                int32_t jl_x = pos.x + (key * (note_size + note_x_spacing));

                olc::Pixel color = draw_judge_colors ? skin->title_gradient_at((double)(jl_x - pos.x) / size.x, (double)(jl_y - pos.y) / size.y) : olc::Pixel(255, 255, 255);
                color.a = get_key(key) ? 255 : judge_alpha;

                window->DrawWarpedDecal(skin->note_decal(key), {{(float)jl_x, (float)jl_y}, {(float)jl_x, (float)(jl_y + note_size)}, {(float)jl_x + (float)note_size, (float)(jl_y + (float)note_size)}, {(float)jl_x + (float)note_size, (float)jl_y}}, with_opacity(color));
            }
        }

        bool evoi_assigned = false;
        double scroll_rate_px_per_ms = ((double)scroll_speed * screen_height / 100.0 / 1000.0);

        // Draw notes
        for (uint32_t i = earliest_visible_object_index; i < chart->hits.size(); i++) {
            const Hit& hit = chart->hits[i];
            bool hit_played = attempt != nullptr && attempt->judged_hits[i].hit_played;

            int32_t hit_y = (hit.time - position) * scroll_rate_px_per_ms;
            int32_t hit_x = pos.x + (hit.key * (note_size + note_x_spacing));
            int32_t hit_height = hit.hit_type == HitType::HOLD ? (hit.end_time - hit.time) * scroll_rate_px_per_ms : 0;

            int32_t screen_hit_y = jl_y - hit_y;

            bool visible = false;

            if ((screen_hit_y + note_size) < pos.y) {
                break;
            }

            if ((screen_hit_y - hit_height) < pos.y + size.y) {
                if (hit.hit_type == HitType::HOLD) {
                    for (double y_offset = 0; y_offset < hit_height; y_offset += (note_size / 4)) {
                        double hold_smear_y = screen_hit_y - hit_height + y_offset;
                        olc::Pixel hold_color = skin->title_gradient_at((double)hit_x / window->GetScreenSize().x, (double)hold_smear_y / window->GetScreenSize().y);
                        hold_color.a = skin->hold_alpha;

                        if ((
                                (!draw_notes_past_judge || hit_played) && hold_smear_y > jl_y
                            ) ||
                            (hold_smear_y >= pos.y + size.y)) {
                            break;
                        }

                        window->DrawWarpedDecal(skin->note_decal(hit.key), {{(float)hit_x, (float)hold_smear_y}, {(float)hit_x, (float)(hold_smear_y + (float)note_size)}, {(float)hit_x + (float)note_size, (float)(hold_smear_y + (float)note_size)}, {(float)hit_x + (float)note_size, (float)hold_smear_y}}, with_opacity(hold_color));
                        visible = true;
                    }
                }

                if (((draw_notes_past_judge && !hit_played) || screen_hit_y <= jl_y) && screen_hit_y < pos.y + size.y) {
                    window->DrawWarpedDecal(skin->note_decal(hit.key), {{(float)hit_x, (float)screen_hit_y}, {(float)hit_x, (float)(screen_hit_y + (float)note_size)}, {(float)hit_x + (float)note_size, (float)(screen_hit_y + (float)note_size)}, {(float)hit_x + (float)note_size, (float)screen_hit_y}}, with_opacity(skin->title_gradient_at((double)hit_x / window->GetScreenSize().x, (double)screen_hit_y / window->GetScreenSize().y)));
                    visible = true;
                }
            }

            if (visible && !evoi_assigned) {
                earliest_visible_object_index = i;
                evoi_assigned = true;
            }
        }

        const JudgedHit* jhit = attempt != nullptr ? attempt->get_last_scored() : nullptr;
        olc::Decal* hs_skin = draw_hit_score && jhit != nullptr ? skin->hit_score_decal(jhit->released ? jhit->release_score : jhit->hit_score) : nullptr;

        if (hs_skin != nullptr) {
            double t = (position - (jhit->hit_played ? (jhit->released ? jhit->attempt_hit.end_time : jhit->attempt_hit.time) : jhit->chart_hit.time + get_window_from_hit_acc_diff(HitScore::BAD, attempt->hit_accuracy))) / 1000;

            float scale = smoothstep(0.8, 0.6, t * 4);

            window->DrawDecal(
                {(float)(pos.x + size.x / 2 - hs_skin->sprite->width * scale / 2),
                 (float)(pos.y + size.y * 0.55 - hs_skin->sprite->height * scale / 2)},
                hs_skin,
                {scale, scale},
                with_opacity(olc::Pixel(255, 255, 255, smoothstep(255, 0, (t - 1) * 2)))
            );

            if (draw_combo) {
                uint32_t combo = attempt->get_combo();

                if (!combo_decal || combo != last_rendered_combo) {
                    combo_decal = SpriteDecal::adopt(skin->display_font_large.RenderStringToDecal(
                        std::format("{}", combo),
                        olc::WHITE
                    ));
                    last_rendered_combo = combo;
                }

                if (combo_decal) {
                    scale = smoothstep(0.6, 0.5, t * 6);

                    window->DrawDecal(
                        {(float)(pos.x + size.x / 2 - combo_decal.get_sprite()->Size().x * scale / 2),
                         (float)(pos.y + size.y * 0.59 - combo_decal.get_sprite()->Size().y * scale / 2)},
                        combo_decal.get(),
                        {scale, scale},
                        with_opacity(olc::WHITE)
                    );
                }
            }
        }

        if (debug) {
            window->DrawStringDecal({(float)(pos.x + 20), (float)(pos.y + 20)}, std::format("POS {:.1f}", position), olc::WHITE, {2.0f, 2.0f});
            window->DrawStringDecal({(float)(pos.x + 20), (float)(pos.y + 40)}, std::format("MB {}", chart->meter_beat_at(position)), olc::WHITE, {2.0f, 2.0f});
            window->DrawStringDecal({(float)(pos.x + 20), (float)(pos.y + 60)}, std::format("SB {}", chart->section_beat_at(position)), olc::WHITE, {2.0f, 2.0f});
        }
    };

    olc::Pixel EntityField::with_opacity(olc::Pixel color) const {
        color.a = (uint8_t)std::lround(color.a * std::clamp(opacity, 0.0f, 1.0f));
        return color;
    }

    double EntityField::get_judge_bottom() const {
        return pos.y + size.y - judge_height;
    }

    void EntityField::set_position(double position) {
        if (position < this->position) {
            earliest_visible_object_index = 0;
        }

        this->position = position;
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

    uint8_t EntityField::get_key_count() {
        return chart->info.key_count;
    }
}
