#include "EntityField.h"
#include "Chart.h"

#include "olcPixelGameEngine.h"

namespace nascent {
    EntityField::EntityField(Chart* chart, Skin* skin, olc::vd2d pos, olc::vd2d size) {
        this->chart = chart;
        this->skin = skin;
        this->pos = pos;
        this->size = size;

        note_size = (size.x) / (chart->info.key_count + chart->info.key_count * FIELD_KEY_SPACING_RATIO - FIELD_KEY_SPACING_RATIO);
        note_x_spacing = note_size * FIELD_KEY_SPACING_RATIO;
    };

    void EntityField::init(Scene* scene) {

    };

    void EntityField::update(Scene* game, float elapsed_time) {
        
    };

    void EntityField::draw(olc::PixelGameEngine* window) {
        int32_t screen_height = window->ScreenHeight();
        const uint8_t key_count = chart->info.key_count;

        // Draw judge line
        int32_t jl_y = pos.y + size.y - note_size;

        for (uint8_t key = 0; key < key_count; key++) {
            int32_t jl_x = pos.x + (key * (note_size + note_x_spacing));
            window->DrawWarpedDecal(skin->note_skin_decals[key],
                {
                    {(float)jl_x, (float)jl_y},
                    {(float)jl_x, (float)(jl_y+note_size)},
                    {(float)jl_x+(float)note_size, (float)(jl_y+(float)note_size)},
                    {(float)jl_x+(float)note_size, (float)jl_y}
                },
                olc::Pixel(255, 255, 255, 64)
            );
        }

        // Draw notes
        for (ChartHit hit : chart->hits) {
            double scroll_rate_px_per_ms = (FIELD_SCROLL_SPEED * screen_height / 100 / 1000);
            
            int32_t hit_y = (hit.time - offset_song_position) * scroll_rate_px_per_ms;
            int32_t hit_x = pos.x + (hit.key * (note_size + note_x_spacing));
            int32_t hit_height = hit.hit_type == HitType::HOLD ? (hit.end_time - hit.time) * scroll_rate_px_per_ms : 0;
            
            int32_t screen_hit_y = jl_y-hit_y;

            if ((screen_hit_y-hit_height) < pos.y+size.y && (screen_hit_y+note_size) > pos.y) {
                if (hit.hit_type == HitType::HOLD) {
                    for (double y_offset = 0; y_offset < hit_height; y_offset += (note_size/4)) {
                        double hold_smear_y = screen_hit_y - hit_height + y_offset;

                        window->DrawWarpedDecal(skin->note_skin_decals[hit.key],
                            {
                                {(float)hit_x, (float)hold_smear_y},
                                {(float)hit_x, (float)(hold_smear_y+(float)note_size)},
                                {(float)hit_x+(float)note_size, (float)(hold_smear_y+(float)note_size)},
                                {(float)hit_x+(float)note_size, (float)hold_smear_y}
                            },
                            olc::Pixel(255, 255, 255, 32)
                        );
                    }
                }

                window->DrawWarpedDecal(skin->note_skin_decals[hit.key],
                    {
                        {(float)hit_x, (float)screen_hit_y},
                        {(float)hit_x, (float)(screen_hit_y+(float)note_size)},
                        {(float)hit_x+(float)note_size, (float)(screen_hit_y+(float)note_size)},
                        {(float)hit_x+(float)note_size, (float)screen_hit_y}
                    },
                    skin->title_gradient_at((double)hit_x/window->GetScreenSize().x, (double)screen_hit_y/window->GetScreenSize().y)
                );
            }
        }

        window->DrawString(window->GetWindowSize().x - 300, 30, std::format("MA POS {}", last_audio_engine_sound_position), olc::WHITE, 2);
        window->DrawString(window->GetWindowSize().x - 300, 50, std::format("EF POS {}", precise_song_position), olc::WHITE, 2);
    };

    void EntityField::update_song_position(int32_t song_position, float elapsed_time) {
        if (song_position != last_audio_engine_sound_position || std::abs(precise_song_position - song_position) > FIELD_AUDIO_SYNC_MAX_ERROR) {
            last_audio_engine_sound_position = song_position;
            precise_song_position = song_position;
        } else {
            precise_song_position += elapsed_time * 1000;
        }

        offset_song_position = precise_song_position + FIELD_DEFAULT_OFFSET_MS;
    }
}
