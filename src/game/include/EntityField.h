#pragma once

#include <cstdint>

#include "Entity.h"
#include "Chart.h"

#define FIELD_KEY_WIDTH 100
#define FIELD_SCROLL_SPEED 200 // screen % height per second
#define FIELD_HIT_MIN_HEIGHT 50

#define FIELD_AUDIO_SYNC_MAX_ERROR 20 // #ms

namespace nascent {
    class EntityField : public Entity {
        protected:
        Chart* chart;
        int32_t last_audio_engine_sound_position = 0;
        double precise_song_position = 0;

        public:
        EntityField(Chart*);
        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;

        void update_song_position(int32_t, float);
    };
}
