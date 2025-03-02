#pragma once

#include <cstdint>

#include "Entity.h"
#include "Chart.h"
#include "Skin.h"
#include "olcPixelGameEngine.h"

#define FIELD_KEY_SPACING_RATIO 0.15
#define FIELD_SCROLL_SPEED 200 // screen % height per second
#define FIELD_HIT_MIN_HEIGHT 50
#define FIELD_DEFAULT_OFFSET_MS 0

#define FIELD_AUDIO_SYNC_MAX_ERROR 20 // #ms

namespace nascent {
    class EntityField : public Entity {
        protected:
        Chart* chart;
        Skin* skin;
        int32_t last_audio_engine_sound_position = 0;
        double precise_song_position = 0;
        double offset_song_position = 0;
        
        double note_size = 0;
        double note_x_spacing = 0;

        public:
        olc::vd2d pos = {0,0};
        olc::vd2d size = {0,0};

        EntityField(Chart*, Skin*, olc::vd2d, olc::vd2d);
        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;

        void update_song_position(int32_t, float);
    };
}
