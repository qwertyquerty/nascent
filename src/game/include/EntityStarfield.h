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

    inline constexpr const char* STARFIELD_PLANET_GRADIENT_PATH = "assets/skins/logo/title_gradient.png";
    inline constexpr int STARFIELD_STAR_COUNT = 9000;
    inline constexpr int STARFIELD_SPRITE_SIZE = 64;
    inline constexpr int STARFIELD_PLANET_SPRITE_SIZE = 640;
    inline constexpr double STARFIELD_FOV = 1.3;
    inline constexpr double STARFIELD_ROTATION_SPEED = 0.03;
    inline constexpr double STARFIELD_BAND_SPREAD = 0.22;
    inline constexpr double STARFIELD_BAND_RATIO = 0.55;
    inline constexpr double STARFIELD_MIN_STAR_SIZE = 3.0;
    inline constexpr double STARFIELD_MAX_STAR_SIZE = 11.0;
    inline constexpr double STARFIELD_FLARE_BRIGHTNESS = 0.9;
    inline constexpr double STARFIELD_SHOOTING_MIN_DELAY = 7.0;
    inline constexpr double STARFIELD_SHOOTING_MAX_DELAY = 18.0;
    inline constexpr double STARFIELD_PLANET_X = 0.77;
    inline constexpr double STARFIELD_PLANET_Y = 0.55;
    inline constexpr double STARFIELD_PLANET_RADIUS = 270.0;
    inline constexpr double STARFIELD_PLANET_BODY_RATIO = 0.4;

    class EntityStarfield : public Entity {
        protected:
        struct Direction {
            double x;
            double y;
            double z;
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
        SpriteDecal planet_body;
        SpriteDecal planet_halo;
        std::vector<Star> stars;
        std::vector<ShootingStar> shooting_stars;
        Direction band_normal = {1, 0, 0};
        Direction rotation_axis = {0, 0, 1};
        Direction planet_reference = {0, 1, 0};
        std::array<double, 9> camera = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        olc::vd2d planet_screen = {0, 0};
        double planet_angle = 0;
        double planet_initial_angle = 0;
        double focal = 1;
        double shooting_timer = 0;
        double time = 0;

        double random(double low, double high);
        static Direction normalized(double x, double y, double z);
        Direction random_direction();
        Direction band_direction();
        olc::Pixel star_tint();
        void spawn_shooting_star();
        void build_sky();
        void build_sprites();
        void build_planet();
        void update_camera();
        void update_planet();
        bool project(const Direction& direction, olc::vd2d& screen, double& depth) const;
        void draw_sprite(olc::PixelGameEngine* window, const SpriteDecal& sprite, olc::vd2d center, double size, olc::Pixel tint) const;
        void draw_star(olc::PixelGameEngine* window, const Star& star) const;
        void draw_shooting_star(olc::PixelGameEngine* window, const ShootingStar& shooting) const;
        void draw_planet(olc::PixelGameEngine* window) const;

        public:
        olc::vd2d size = {0, 0};

        EntityStarfield(Skin*, olc::vd2d);

        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
