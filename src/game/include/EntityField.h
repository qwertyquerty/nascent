#pragma once

#include <cstdint>

#include "Entity.h"
#include "Chart.h"
#include "Skin.h"
#include "Attempt.h"
#include "olcPixelGameEngine.h"

#define FIELD_KEY_SPACING_RATIO 0.15
#define FIELD_SCROLL_SPEED 200 // screen % height per second
#define FIELD_HIT_MIN_HEIGHT 50
#define FIELD_DEFAULT_OFFSET_MS 0

#define FIELD_AUDIO_SYNC_MAX_ERROR 20 //ms

namespace nascent {
    class EntityField : public Entity {
        protected:
        Chart* chart;
        Attempt* attempt;
        Skin* skin;
        int32_t last_audio_engine_sound_position = 0;
        double precise_song_position = 0;
        double offset_song_position = 0;
        
        double note_size = 0;
        double note_x_spacing = 0;

        public:
        bool debug = false;
        bool draw_judge = true;
        bool draw_notes_past_judge = true;
        bool draw_judge_colors = false;
        bool judge_auto_active = false;

        uint8_t judge_alpha = 64;

        uint16_t active_keys = 0;

        olc::vd2d pos = {0,0};
        olc::vd2d size = {0,0};

        EntityField(Chart*, Skin*, olc::vd2d, olc::vd2d);
        ~EntityField();
        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;

        void update_song_position(int32_t, float);

        ChartTimingPoint* get_current_timing_point();
        uint16_t get_section_beat();
        uint8_t get_meter_beat();

        void set_key(uint8_t key);
        void clear_key(uint8_t key);
        void clear_keys();
        bool get_key(uint8_t key);
        uint8_t get_key_count();
    };
}
