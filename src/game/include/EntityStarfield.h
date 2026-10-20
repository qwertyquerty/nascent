#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "olcPixelGameEngine.h"
#include "gse/render/MipDecal.h"

namespace nascent {
    class Skin;

    enum class SkyView : uint8_t {
        TITLE,
        ACCOUNT
    };

    inline constexpr size_t SKY_VIEW_COUNT = 2;
    inline constexpr int STARFIELD_STAR_COUNT = 9000;
    inline constexpr int STARFIELD_SPRITE_SIZE = 64;
    inline constexpr int STARFIELD_PLANET_SPRITE_SIZE = 640;
    inline constexpr double STARFIELD_PLANET_BODY_RATIO = 0.4;

    class EntityStarfield {
        public:
        struct Direction {
            double x;
            double y;
            double z;
        };

        struct Quaternion {
            double w;
            double x;
            double y;
            double z;
        };

        protected:
        using Matrix = std::array<double, 9>;

        struct PlanetLook {
            bool visible;
            double light_level;
            const char* gradient_path;
            double gradient_start;
            double gradient_end;
            double screen_x;
            double screen_y;
            double radius;
            double pitch;
            Direction light;
            double band_frequency;
            double pole_tilt;
        };

        struct Planet {
            Direction direction;
            Direction north;
            double angular_radius = 0;
            MipDecal body;
            MipDecal halo;
        };

        struct View {
            Matrix orientation;
            Direction spin_axis;
            double spin_start = 0;
        };

        struct Star {
            Direction direction;
            double brightness;
            double phase;
            double twinkle_rate;
            olc::Pixel tint;
        };

        struct StarStyle {
            double min_size;
            double max_size;
            double flare_brightness;
            double flare_size;
        };

        struct ShootingStar {
            olc::vd2d pos;
            olc::vd2d velocity;
            double age;
            double life;
            double length;
            olc::Pixel tint;
        };

        Skin* skin;
        std::mt19937 rng;
        MipDecal glow;
        MipDecal flare;
        MipDecal streak;
        std::array<Planet, SKY_VIEW_COUNT> planets;
        std::array<View, SKY_VIEW_COUNT> views;
        std::vector<Star> stars;
        std::vector<ShootingStar> shooting_stars;
        Direction band_normal = {1, 0, 0};
        Matrix camera = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        SkyView view = SkyView::TITLE;
        Quaternion pan_from = {1, 0, 0, 0};
        double pan_from_light_level = 1;
        double light_level = 1;
        double pan_start = 0;
        double pan_duration = 0;
        double focal = 1;
        double shooting_timer = 0;
        double time = 0;

        static const std::array<PlanetLook, SKY_VIEW_COUNT> PLANET_LOOKS;

        double random(double low, double high);
        static Direction normalized(double x, double y, double z);
        Direction random_direction();
        Direction band_direction(double spread);
        Direction screen_direction(double screen_x, double screen_y) const;
        olc::Pixel star_tint();
        void spawn_shooting_star();
        void build_sky();
        void build_sprites();
        void build_planet(Planet& planet, const PlanetLook& look);
        Matrix view_matrix(SkyView target) const;
        void place_planet(SkyView target);
        void anchor_view(SkyView target);
        void update_camera();
        bool project(const Direction& direction, olc::vd2d& screen, double& depth) const;
        void draw_sprite(olc::PixelGameEngine* window, const MipDecal& sprite, olc::vd2d center, double size, olc::Pixel tint) const;
        void draw_star(olc::PixelGameEngine* window, const Star& star, const StarStyle& style) const;
        void draw_shooting_star(olc::PixelGameEngine* window, const ShootingStar& shooting) const;
        void draw_planet(olc::PixelGameEngine* window, const Planet& planet) const;

        static constexpr int32_t STREAK_WIDTH = 4;
        static constexpr int32_t STREAK_HEIGHT = 32;

        public:
        olc::vd2d size = {0, 0};

        EntityStarfield(Skin*, olc::vd2d);

        void show(SkyView target);
        void pan_to(SkyView target);
        void pan_to(SkyView target, double seconds);

        void init();
        void update(float elapsed_time);
        void draw(olc::PixelGameEngine*);
    };
}
