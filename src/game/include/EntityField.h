#pragma once

#include <cstdint>

#include "Entity.h"
#include "Util.h"
#include "olcPixelGameEngine.h"

namespace nascent {
    class Attempt;
    class Chart;
    class Skin;

    class EntityField : public Entity {
        protected:
        double note_size = 0;
        double note_x_spacing = 0;

        uint32_t earliest_visible_object_index = 0;

        SpriteDecal combo_decal;
        uint32_t last_rendered_combo = 0;

        public:
        const Chart* chart;
        const Attempt* attempt = nullptr;
        Skin* skin;

        bool debug = false;
        bool draw_judge = true;
        bool draw_notes_past_judge = true;
        bool draw_judge_colors = false;
        bool draw_combo = false;
        double judge_height = 0;

        uint8_t judge_alpha = 64;

        uint32_t active_keys = 0;

        double position = 0;
        float opacity = 1.0f;
        uint32_t scroll_speed = 0;

        olc::vd2d pos = {0, 0};
        olc::vd2d size = {0, 0};

        EntityField(const Chart*, Skin*, olc::vd2d, olc::vd2d);
        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;

        void set_position(double position);
        double get_judge_bottom() const;
        olc::Pixel with_opacity(olc::Pixel color) const;
        void set_attempt(const Attempt* attempt);

        void set_keys(uint32_t keys);
        void set_key(uint8_t key);
        void clear_keys();
        bool get_key(uint8_t key);
        uint8_t get_key_count();
    };
}
