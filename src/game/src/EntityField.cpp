#include "EntityField.h"
#include "Chart.h"

#include "olcPixelGameEngine.h"

namespace nascent {
    EntityField::EntityField(Chart* chart) {
        this->chart = chart;
    };

    void EntityField::init(Scene* scene) {

    };

    void EntityField::update(Scene* game, float elapsed_time) {
        
    };

    void EntityField::draw(olc::PixelGameEngine* window) {
        for (ChartHit hit : chart->hits) {
            int32_t hit_y = (hit.time - precise_song_position) * (FIELD_SCROLL_SPEED * window->ScreenHeight() / 100 / 1000);
            int32_t hit_x = (hit.key * FIELD_KEY_WIDTH);
            int32_t hit_width = FIELD_KEY_WIDTH;
            int32_t hit_height = hit.hit_type == HitType::HOLD ? 
                (hit.end_time - hit.time) * (FIELD_SCROLL_SPEED * window->ScreenHeight() / 100 / 1000) + FIELD_HIT_MIN_HEIGHT : FIELD_HIT_MIN_HEIGHT;
            
            window->FillRect(hit_x, window->ScreenHeight()-hit_y-hit_height, hit_width, hit_height, olc::WHITE);
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
    }
}
