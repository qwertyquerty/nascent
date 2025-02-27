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
            int32_t hit_y = (hit.time - song_position) * (FIELD_SCROLL_SPEED * window->ScreenHeight() / 100 / 1000);
            int32_t hit_x = (hit.key * FIELD_KEY_WIDTH);
            int32_t hit_width = FIELD_KEY_WIDTH;
            int32_t hit_height = hit.hit_type == HitType::HOLD ? (hit.end_time - hit.time) * (FIELD_SCROLL_SPEED * window->ScreenHeight() / 100 / 1000) : FIELD_HIT_MIN_HEIGHT;
            
            window->FillRect(hit_x, window->ScreenHeight()-hit_y-hit_height, hit_width, hit_height, olc::WHITE);
        }
    };

    void EntityField::update_song_position(int32_t song_position) {
        this->song_position = song_position;
    }
}
