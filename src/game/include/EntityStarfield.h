#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <vector>

#include "Entity.h"
#include "Util.h"
#include "olcPixelGameEngine.h"

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
    inline constexpr double STARFIELD_FOV = 1.3;
    inline constexpr double STARFIELD_ROTATION_SPEED = 0.03;
    inline constexpr double STARFIELD_PAN_TIME = 1.5;
    inline constexpr double STARFIELD_BAND_SPREAD = 0.22;
    inline constexpr double STARFIELD_BAND_RATIO = 0.55;
    inline constexpr double STARFIELD_MIN_STAR_SIZE = 3.0;
    inline constexpr double STARFIELD_MAX_STAR_SIZE = 11.0;
    inline constexpr double STARFIELD_FLARE_BRIGHTNESS = 0.9;
    inline constexpr double STARFIELD_SHOOTING_MIN_DELAY = 7.0;
    inline constexpr double STARFIELD_SHOOTING_MAX_DELAY = 18.0;

    class EntityStarfield : public Entity {
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
            SpriteDecal body;
            SpriteDecal halo;
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
        SpriteDecal glow;
        SpriteDecal flare;
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
        Direction band_direction();
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
        void draw_sprite(olc::PixelGameEngine* window, const SpriteDecal& sprite, olc::vd2d center, double size, olc::Pixel tint) const;
        void draw_star(olc::PixelGameEngine* window, const Star& star) const;
        void draw_shooting_star(olc::PixelGameEngine* window, const ShootingStar& shooting) const;
        void draw_planet(olc::PixelGameEngine* window, const Planet& planet) const;

        public:
        olc::vd2d size = {0, 0};

        EntityStarfield(Skin*, olc::vd2d);

        void show(SkyView target);
        void pan_to(SkyView target, double seconds = STARFIELD_PAN_TIME);

        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
