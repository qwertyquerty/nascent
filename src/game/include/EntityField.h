#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "olcPixelGameEngine.h"
#include "gse/render/SpriteDecal.h"

namespace nascent {
    class Attempt;
    struct JudgedHit;
    class Chart;
    class Skin;
    class MipDecal;
    enum class SkinImage : uint8_t;

    class EntityField {
        protected:
        double note_size = 0;
        double note_x_spacing = 0;

        int32_t longest_hold_ms = 0;
        float longest_hold_pitch = 0;
        std::vector<int32_t> latest_ends;

        void update_longest_hold();
        size_t first_drawn_hit(double rate_px_per_ms) const;

        std::vector<uint8_t> snaps;
        float snaps_pitch = 0;
        std::vector<bool> pressed_lanes;
        std::vector<double> press_positions;
        std::vector<double> release_positions;

        void update_snaps();
        void update_presses();
        float lane_light_level(uint8_t lane) const;
        void draw_stage(olc::PixelGameEngine* window, int32_t jl_y);
        void draw_lane_lights(olc::PixelGameEngine* window, int32_t jl_y);
        void draw_judgement_line(olc::PixelGameEngine* window, int32_t jl_y);
        void draw_stretched(olc::PixelGameEngine* window, const MipDecal* image, double x, double top, double width, double height, olc::Pixel tint) const;
        olc::Pixel hold_tint(olc::Pixel color, bool dropped) const;

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
        float note_opacity = 1.0f;
        float receptor_opacity = 1.0f;

        uint32_t active_keys = 0;

        double position = 0;
        float opacity = 1.0f;
        uint32_t scroll_speed = 0;

        olc::vd2d pos = {0, 0};
        olc::vd2d size = {0, 0};

        EntityField(const Chart*, Skin*, olc::vd2d, olc::vd2d);
        void draw(olc::PixelGameEngine*);

        void set_position(double position);
        void set_lane_gap(std::optional<double> gap);
        double get_judge_bottom() const;
        double flip_y(double y) const;
        double overlay_y(double height) const;
        olc::Pixel with_opacity(olc::Pixel color) const;
        olc::Pixel note_tint(olc::Pixel color) const;
        void draw_judgement(olc::PixelGameEngine* window, const JudgedHit& judged, double t);
        olc::Pixel note_color(double x, double y, olc::PixelGameEngine* window) const;
        olc::Decal* sized_image(SkinImage image, uint8_t column, uint8_t key_count, std::optional<uint8_t> snap = std::nullopt, std::optional<double> since = std::nullopt) const;
        double layout(std::string_view key) const;
        void draw_square(olc::PixelGameEngine* window, olc::Decal* decal, double x, double y, olc::Pixel tint) const;
        void draw_hold_body(olc::PixelGameEngine* window, olc::Decal* body, double x, double tail, double head, olc::Pixel tint) const;
        void draw_lane_cover(olc::PixelGameEngine* window, double judge_y) const;
        void draw_receptors(olc::PixelGameEngine* window, int32_t jl_y);
        void set_attempt(const Attempt* attempt);

        void set_keys(uint32_t keys);
        void set_key(uint8_t key);
        void clear_keys();
        bool get_key(uint8_t key);
        uint8_t get_key_count();
        uint8_t display_column(uint8_t key) const;
    };
}
