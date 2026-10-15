#pragma once

#include "gse/render/MipDecal.h"
#include "olcPixelGameEngine.h"

#include <cstddef>
#include <functional>
#include <random>
#include <vector>

namespace nascent {
    struct ParticleBurst {
        olc::vf2d center;
        size_t count = 0;
        float speed_min = 0;
        float speed_max = 0;
        float lift = 0;
        float life_min = 1;
        float life_max = 1;
        float size_min = 1;
        float size_max = 1;
        std::function<olc::Pixel(std::mt19937&)> color;
    };

    class ParticleEmitter {
        protected:
        struct Particle {
            olc::vf2d position;
            olc::vf2d velocity;
            float life = 0;
            float lifetime = 1;
            float size = 1;
            olc::Pixel color;
        };

        std::vector<Particle> particles;
        std::mt19937 random;
        mutable MipDecal dot;

        public:
        float gravity = 0;
        float drag = 0;

        ParticleEmitter();

        void burst(const ParticleBurst& burst);
        void update(float elapsed_time);
        static constexpr int32_t DOT_SIZE = 32;
        static constexpr int32_t DOT_SIDES = 32;

        void draw(olc::PixelGameEngine* window) const;
        size_t count() const;
        void clear();
    };
}
