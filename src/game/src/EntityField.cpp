#include "EntityField.h"
#include "Chart.h"
#include "Util.h"

#include "olcPixelGameEngine.h"

namespace nascent {
    EntityField::EntityField(Chart* chart, Skin* skin, olc::vd2d pos, olc::vd2d size) {
        this->chart = chart;
        this->skin = skin;
        this->pos = pos;
        this->size = size;

        scroll_speed = SettingsManager::settings.field_scroll_speed;
        
        attempt = new Attempt(this->chart);

        note_size = (size.x) / (chart->info.key_count + chart->info.key_count * skin->field_key_spacing_ratio - skin->field_key_spacing_ratio);
        note_x_spacing = note_size * skin->field_key_spacing_ratio;

        active_keys = 0;
    };

    EntityField::~EntityField() {
        delete attempt;
    }

    void EntityField::init(Scene* scene) {

    };

    void EntityField::update(Scene* scene, float elapsed_time) {
        if (judge_auto_active) {
            clear_keys();
            for (Hit hit : chart->hits) {
                if (hit.hit_type == HitType::HOLD) {
                    if (hit.time <= offset_song_position && hit.end_time > offset_song_position) {
                        set_key(hit.key);
                    }
                }
            }
        }

        attempt->update((uint32_t)(precise_song_position + audio_input_offset), active_keys);
    };

    void EntityField::draw(olc::PixelGameEngine* window) {
        int32_t screen_height = window->ScreenHeight();
        int32_t screen_width = window->ScreenWidth();
        const uint8_t key_count = chart->info.key_count;

        // Draw judge line
        int32_t jl_y = pos.y + size.y - note_size - judge_height;

        if (draw_judge) {
            for (uint8_t key = 0; key < key_count; key++) {
                int32_t jl_x = pos.x + (key * (note_size + note_x_spacing));

                olc::Pixel color = draw_judge_colors ? skin->title_gradient_at((double)(jl_x-pos.x)/size.x, (double)(jl_y-pos.y)/size.y) : olc::Pixel(255, 255, 255);
                color.a = get_key(key) ? 255 : judge_alpha;
                
                window->DrawWarpedDecal(skin->note_skin_decals[key],
                    {
                        {(float)jl_x, (float)jl_y},
                        {(float)jl_x, (float)(jl_y+note_size)},
                        {(float)jl_x+(float)note_size, (float)(jl_y+(float)note_size)},
                        {(float)jl_x+(float)note_size, (float)jl_y}
                    },
                    color
                );
            }
        }

        bool evoi_assigned = false;

        // Draw notes
        for (uint32_t i = earliest_visible_object_index; i < attempt->judged_hits.size(); i++) {
            JudgedHit* jhit = attempt->judged_hits[i];

            Hit hit = jhit->chart_hit;

            double scroll_rate_px_per_ms = ((double)scroll_speed / chart_pitch * screen_height / 100.0 / 1000.0);
            
            int32_t hit_y = (hit.time - offset_song_position) * scroll_rate_px_per_ms;
            int32_t hit_x = pos.x + (hit.key * (note_size + note_x_spacing));
            int32_t hit_height = hit.hit_type == HitType::HOLD ? (hit.end_time - hit.time) * scroll_rate_px_per_ms : 0;
            
            int32_t screen_hit_y = jl_y-hit_y;

            bool visible = false;

            if ((screen_hit_y+note_size) < pos.y) {
                break;
            }

            if ((screen_hit_y-hit_height) < pos.y+size.y) {
                if (hit.hit_type == HitType::HOLD) {
                    for (double y_offset = 0; y_offset < hit_height; y_offset += (note_size/4)) {
                        double hold_smear_y = screen_hit_y - hit_height + y_offset;
                        olc::Pixel hold_color = skin->title_gradient_at((double)hit_x/window->GetScreenSize().x, (double)hold_smear_y/window->GetScreenSize().y);
                        hold_color.a = skin->hold_alpha;

                        if ((
                            (!draw_notes_past_judge || jhit->hit_played) && hold_smear_y > jl_y)
                            ||
                            (hold_smear_y >= pos.y+size.y)
                        ) {
                            break;
                        }

                        window->DrawWarpedDecal(skin->note_skin_decals[hit.key],
                            {
                                {(float)hit_x, (float)hold_smear_y},
                                {(float)hit_x, (float)(hold_smear_y+(float)note_size)},
                                {(float)hit_x+(float)note_size, (float)(hold_smear_y+(float)note_size)},
                                {(float)hit_x+(float)note_size, (float)hold_smear_y}
                            },
                            hold_color
                        );
                        visible = true;
                    }
                }

                if (((draw_notes_past_judge && !jhit->hit_played) || screen_hit_y <= jl_y) && screen_hit_y < pos.y+size.y) {
                    window->DrawWarpedDecal(skin->note_skin_decals[hit.key],
                        {
                            {(float)hit_x, (float)screen_hit_y},
                            {(float)hit_x, (float)(screen_hit_y+(float)note_size)},
                            {(float)hit_x+(float)note_size, (float)(screen_hit_y+(float)note_size)},
                            {(float)hit_x+(float)note_size, (float)screen_hit_y}
                        },
                        skin->title_gradient_at((double)hit_x/window->GetScreenSize().x, (double)screen_hit_y/window->GetScreenSize().y)
                    );
                    visible = true;
                }
            }

            if (visible && !evoi_assigned) {
                earliest_visible_object_index = jhit->index;
                evoi_assigned = true;
            }
        }

        if (attempt->last_scored_object != nullptr) {
            JudgedHit* jhit = attempt->last_scored_object;
            HitScore hs = jhit->released ? jhit->release_score : jhit->hit_score;
            olc::Decal* hs_skin = skin->hit_score_decals[hs];

            double t = (precise_song_position - (
                jhit->hit_played ?
                    (jhit->released ? jhit->attempt_hit.end_time : jhit->attempt_hit.time) :
                    jhit->chart_hit.time + get_window_from_hit_acc_diff(HitScore::BAD, chart->info.hit_accuracy) 
            )) / 1000;
            
            float scale = smoothstep(1.0, 0.75, t*4);

            window->DrawDecal(
                {(double)pos.x+size.x/2 - (double)hs_skin->sprite->width*scale/2, (double)pos.y+size.y/2 - (double)hs_skin->sprite->height*scale/2},
                hs_skin,
                {scale, scale},
                olc::Pixel(255, 255, 255, smoothstep(255, 0, (t-1)*2))
            );
        }

        if (debug) {
            window->DrawString(pos.x+20, pos.y+20, std::format("MA POS {}", last_audio_engine_sound_position), olc::WHITE, 2);
            window->DrawString(pos.x+20, pos.y+40, std::format("EF POS {}", precise_song_position), olc::WHITE, 2);
            window->DrawString(pos.x+20, pos.y+60, std::format("MB {}", get_meter_beat()), olc::WHITE, 2);
            window->DrawString(pos.x+20, pos.y+80, std::format("SB {}", get_section_beat()), olc::WHITE, 2);
        }
    };

    void EntityField::update_song_position(int32_t song_position, float elapsed_time) {
        if (song_position != last_audio_engine_sound_position || std::abs(precise_song_position - song_position) > SettingsManager::settings.field_audio_sync_max_error) {
            if (song_position < last_audio_engine_sound_position) {
                earliest_visible_object_index = 0;
            }
            
            last_audio_engine_sound_position = song_position;
            precise_song_position = song_position;
        } else {
            precise_song_position += elapsed_time * 1000;
        }

        offset_song_position = precise_song_position + audio_visual_offset;
    }

    ChartTimingPoint* EntityField::get_current_timing_point() {
        for (int16_t i = chart->timing_points.size() - 1; i >= 0; i--) {
            if (chart->timing_points[i].time <= precise_song_position) {
                return &chart->timing_points[i];
            }
        }

        return nullptr;
    }

    uint16_t EntityField::get_section_beat() {
        ChartTimingPoint* tp = get_current_timing_point();
        return (tp != nullptr) ? (precise_song_position - tp->time) / tp->beat_length : 0;
    }

    uint8_t EntityField::get_meter_beat() {
        ChartTimingPoint* tp = get_current_timing_point();
        return (tp != nullptr) ? (uint16_t)((precise_song_position - tp->time) / tp->beat_length) % tp->meter : 0;
    }

    void EntityField::set_keys(uint32_t keys) {
        active_keys = keys;
    }

    void EntityField::set_key(uint8_t key) {
        active_keys |= (1 << key);
    }

    void EntityField::clear_key(uint8_t key) {
        active_keys &= ~(1 << key);
    }

    void EntityField::clear_keys() {
        active_keys = 0;
    }

    bool EntityField::get_key(uint8_t key) {
        return active_keys & (1 << key);
    }

    uint8_t EntityField::get_key_count() {
        return chart->info.key_count;
    }
}
