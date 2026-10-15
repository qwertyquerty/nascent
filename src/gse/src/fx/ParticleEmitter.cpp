#include "gse/fx/ParticleEmitter.h"

#include "gse/render/Color.h"
#include "gse/render/ShapeSprite.h"

#include <cmath>
#include <numbers>

namespace nascent {
    ParticleEmitter::ParticleEmitter() : random(std::random_device{}()) {}

    void ParticleEmitter::burst(const ParticleBurst& burst) {
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        for (size_t index = 0; index < burst.count; index++) {
            float angle = unit(random) * 2.0f * std::numbers::pi_v<float>;
            float speed = burst.speed_min + (burst.speed_max - burst.speed_min) * unit(random);

            Particle particle;
            particle.position = burst.center;
            particle.velocity = {std::cos(angle) * speed, std::sin(angle) * speed - burst.lift};
            particle.lifetime = burst.life_min + (burst.life_max - burst.life_min) * unit(random);
            particle.size = burst.size_min + (burst.size_max - burst.size_min) * unit(random);
            particle.color = burst.color ? burst.color(random) : olc::WHITE;
            particles.push_back(particle);
        }
    }

    void ParticleEmitter::update(float elapsed_time) {
        float damping = std::exp(-drag * elapsed_time);

        for (Particle& particle : particles) {
            particle.life += elapsed_time;
            particle.velocity = particle.velocity * damping + olc::vf2d{0.0f, gravity * elapsed_time};
            particle.position += particle.velocity * elapsed_time;
        }

        std::erase_if(particles, [](const Particle& particle) { return particle.life >= particle.lifetime; });
    }

    void ParticleEmitter::draw(olc::PixelGameEngine* window) const {
        if (particles.empty()) {
            return;
        }

        if (!dot) {
            float half = DOT_SIZE / 2.0f;
            dot = MipDecal(rasterize_polygon(star_points({half, half}, half, half, DOT_SIDES / 2), {DOT_SIZE, DOT_SIZE}));
        }

        for (const Particle& particle : particles) {
            float remaining = 1.0f - particle.life / particle.lifetime;
            float size = particle.size * (0.4f + 0.6f * remaining);
            MipDecal::Pick level = dot.pick(size / DOT_SIZE);
            float scale = size / DOT_SIZE / level.factor;
            window->DrawDecal(particle.position - olc::vf2d{size / 2.0f, size / 2.0f}, level.decal, {scale, scale}, fade(particle.color, remaining));
        }
    }

    size_t ParticleEmitter::count() const {
        return particles.size();
    }

    void ParticleEmitter::clear() {
        particles.clear();
    }
}
