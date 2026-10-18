#pragma once

#include <cstdint>
#include <optional>

#include "olcPixelGameEngine.h"
#include "gse/render/SpriteDecal.h"

namespace nascent {
    class Attempt;
    class Chart;
    class Skin;
    enum class SkinImage : uint8_t;

    class EntityField {
        protected:
        double note_size = 0;
        double note_x_spacing = 0;

        int32_t longest_hold_ms = 0;
        float longest_hold_pitch = 0;

        void update_longest_hold();

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
        bool note_gradient = true;
        std::vector<float> lane_light;
        bool draw_hit_score = true;
        bool draw_combo = false;
        double judge_height = 0;
        bool upscroll = false;
        bool mirror = false;
        double judgement_height = 0.45;
        double combo_height = 0.41;
        double lane_cover = 0;

        uint8_t judge_alpha = 64;

        uint32_t active_keys = 0;

        double position = 0;
        float opacity = 1.0f;
        uint32_t scroll_speed = 0;

        olc::vd2d pos = {0, 0};
        olc::vd2d size = {0, 0};

        static constexpr float LANE_COVER_FADE = 40.0f;

        EntityField(const Chart*, Skin*, olc::vd2d, olc::vd2d);
        void draw(olc::PixelGameEngine*);

        void set_position(double position);
        void set_lane_gap(std::optional<double> gap);
        double get_judge_bottom() const;
        double flip_y(double y) const;
        double overlay_y(double height) const;
        olc::Pixel with_opacity(olc::Pixel color) const;
        olc::Pixel note_color(double x, double y, olc::PixelGameEngine* window) const;
        olc::Decal* sized_image(SkinImage image, uint8_t column, uint8_t key_count) const;
        void draw_square(olc::PixelGameEngine* window, olc::Decal* decal, double x, double y, olc::Pixel tint) const;
        void draw_hold_body(olc::PixelGameEngine* window, olc::Decal* body, double x, double tail, double head, olc::Pixel tint) const;
        void draw_lane_cover(olc::PixelGameEngine* window, double judge_y) const;
        void set_attempt(const Attempt* attempt);

        void set_keys(uint32_t keys);
        void set_key(uint8_t key);
        void clear_keys();
        bool get_key(uint8_t key);
        uint8_t get_key_count();
        uint8_t display_column(uint8_t key) const;
    };
}
