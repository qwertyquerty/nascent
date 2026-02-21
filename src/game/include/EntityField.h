#pragma once

#include <cstdint>

#include "Entity.h"
#include "Chart.h"
#include "Skin.h"
#include "Attempt.h"
#include "olcPixelGameEngine.h"
#include "SettingsManager.h"

namespace nascent {
    class EntityField : public Entity {
        protected:
        int32_t last_audio_engine_sound_position = 0;
        double precise_song_position = 0;
        double offset_song_position = 0;
        
        double note_size = 0;
        double note_x_spacing = 0;

        uint32_t earliest_visible_object_index = 0;

        public:
        Chart* chart;
        Attempt* attempt;
        Skin* skin;

        bool debug = true;
        bool draw_judge = true;
        bool draw_notes_past_judge = true;
        bool draw_judge_colors = false;
        bool judge_auto_active = false;
        double judge_height = 0;

        uint8_t judge_alpha = 64;

        uint32_t active_keys = 0;

        int32_t audio_visual_offset = 0;
        int32_t audio_input_offset = 0;
        uint32_t scroll_speed = 0;

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

        void set_keys(uint32_t keys);
        void set_key(uint8_t key);
        void clear_key(uint8_t key);
        void clear_keys();
        bool get_key(uint8_t key);
        uint8_t get_key_count();
    };
}
