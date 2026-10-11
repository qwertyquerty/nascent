#include "EntityStarfield.h"
#include "Skin.h"
#include "Util.h"

#include "olcPixelGameEngine.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace nascent {
    static uint8_t alpha_of(double amount) {
        return (uint8_t)std::clamp(amount * 255, 0.0, 255.0);
    }

    static double lattice_value(int x, int y, uint32_t seed) {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return (double)(h & 0xFFFFFF) / (double)0xFFFFFF;
    }

    static double value_noise(double x, double y, uint32_t seed) {
        int x0 = (int)std::floor(x);
        int y0 = (int)std::floor(y);
        double fx = x - x0;
        double fy = y - y0;
        double sx = fx * fx * (3 - 2 * fx);
        double sy = fy * fy * (3 - 2 * fy);

        double top = std::lerp(lattice_value(x0, y0, seed), lattice_value(x0 + 1, y0, seed), sx);
        double bottom = std::lerp(lattice_value(x0, y0 + 1, seed), lattice_value(x0 + 1, y0 + 1, seed), sx);
        return std::lerp(top, bottom, sy);
    }

    EntityStarfield::EntityStarfield(Skin* skin, olc::vd2d size) {
        this->skin = skin;
        this->size = size;
        rng.seed(std::random_device{}());
    }

    double EntityStarfield::random(double low, double high) {
        return std::uniform_real_distribution<double>(low, high)(rng);
    }

    EntityStarfield::Direction EntityStarfield::normalized(double x, double y, double z) {
        double length = std::sqrt(x * x + y * y + z * z);
        return {x / length, y / length, z / length};
    }

    EntityStarfield::Direction EntityStarfield::random_direction() {
        std::normal_distribution<double> normal(0, 1);
        return normalized(normal(rng), normal(rng), normal(rng));
    }

    EntityStarfield::Direction EntityStarfield::band_direction() {
        const Direction& n = band_normal;
        Direction reference = std::abs(n.x) < 0.9 ? Direction{1, 0, 0} : Direction{0, 1, 0};
        Direction u = normalized(n.y * reference.z - n.z * reference.y, n.z * reference.x - n.x * reference.z, n.x * reference.y - n.y * reference.x);
        Direction w = {n.y * u.z - n.z * u.y, n.z * u.x - n.x * u.z, n.x * u.y - n.y * u.x};

        double around = random(0, 6.283185307);
        double offset = std::normal_distribution<double>(0, STARFIELD_BAND_SPREAD)(rng);
        double ring = std::cos(offset);
        double lift = std::sin(offset);

        return normalized(
            ring * (std::cos(around) * u.x + std::sin(around) * w.x) + lift * n.x,
            ring * (std::cos(around) * u.y + std::sin(around) * w.y) + lift * n.y,
            ring * (std::cos(around) * u.z + std::sin(around) * w.z) + lift * n.z
        );
    }

    olc::Pixel EntityStarfield::star_tint() {
        olc::Pixel color = skin->title_gradient_at(random(0, 1), random(0, 1));
        double whiteness = random(0.55, 0.9);
        return olc::Pixel(
            (uint8_t)std::lerp((double)color.r, 255.0, whiteness),
            (uint8_t)std::lerp((double)color.g, 255.0, whiteness),
            (uint8_t)std::lerp((double)color.b, 255.0, whiteness)
        );
    }

    void EntityStarfield::spawn_shooting_star() {
        double angle = random(0.22, 0.5);
        double speed = random(900, 1500);

        ShootingStar shooting;
        shooting.pos = {random(size.x * 0.3, size.x * 1.1), random(-size.y * 0.1, size.y * 0.45)};
        shooting.velocity = {-std::cos(angle) * speed, std::sin(angle) * speed};
        shooting.age = 0;
        shooting.life = random(0.55, 1.1);
        shooting.length = random(140, 300);
        shooting.tint = star_tint();
        shooting_stars.push_back(shooting);
    }

    void EntityStarfield::build_sky() {
        double band_angle = random(0, 3.14159265);
        band_normal = {std::cos(band_angle), std::sin(band_angle), 0};

        stars.clear();

        for (int i = 0; i < STARFIELD_STAR_COUNT; i++) {
            Star star;
            star.direction = random(0, 1) < STARFIELD_BAND_RATIO ? band_direction() : random_direction();
            star.brightness = std::pow(random(0, 1), 4.0);
            star.phase = random(0, 6.283185307);
            star.twinkle_rate = random(0.6, 3.2);
            star.tint = star_tint();
            stars.push_back(star);
        }
    }

    void EntityStarfield::build_sprites() {
        std::unique_ptr<olc::Sprite> glow_sprite = std::make_unique<olc::Sprite>(STARFIELD_SPRITE_SIZE, STARFIELD_SPRITE_SIZE);
        std::unique_ptr<olc::Sprite> flare_sprite = std::make_unique<olc::Sprite>(STARFIELD_SPRITE_SIZE, STARFIELD_SPRITE_SIZE);
        double half = STARFIELD_SPRITE_SIZE / 2.0;

        for (int32_t y = 0; y < STARFIELD_SPRITE_SIZE; y++) {
            for (int32_t x = 0; x < STARFIELD_SPRITE_SIZE; x++) {
                double dx = (x + 0.5 - half) / half;
                double dy = (y + 0.5 - half) / half;
                double r2 = dx * dx + dy * dy;
                double edge = std::clamp(1 - r2, 0.0, 1.0);

                double core = (std::exp(-r2 * 28) + 0.3 * std::exp(-r2 * 6)) * edge;
                double arms = (std::exp(-dy * dy * 400) * (1 - std::abs(dx)) + std::exp(-dx * dx * 400) * (1 - std::abs(dy))) * edge;

                glow_sprite->SetPixel(x, y, olc::Pixel(255, 255, 255, alpha_of(core)));
                flare_sprite->SetPixel(x, y, olc::Pixel(255, 255, 255, alpha_of(arms)));
            }
        }

        glow = SpriteDecal(std::move(glow_sprite), true);
        flare = SpriteDecal(std::move(flare_sprite), true);
    }

    void EntityStarfield::build_planet() {
        std::unique_ptr<olc::Sprite> body_sprite = std::make_unique<olc::Sprite>(STARFIELD_PLANET_SPRITE_SIZE, STARFIELD_PLANET_SPRITE_SIZE);
        std::unique_ptr<olc::Sprite> halo_sprite = std::make_unique<olc::Sprite>(STARFIELD_PLANET_SPRITE_SIZE, STARFIELD_PLANET_SPRITE_SIZE);
        double half = STARFIELD_PLANET_SPRITE_SIZE / 2.0;
        double body_radius = STARFIELD_PLANET_SPRITE_SIZE * STARFIELD_PLANET_BODY_RATIO;
        uint32_t seed = (uint32_t)rng();

        Direction light = normalized(-0.75, -0.35, 0.55);
        Direction pole = normalized(-0.36, -0.88, 0.3);
        double light_flat = std::sqrt(light.x * light.x + light.y * light.y);

        olc::Sprite gradient(STARFIELD_PLANET_GRADIENT_PATH);
        auto planet_color = [&](double t) {
            t = std::clamp(t, 0.0, 1.0);

            if (gradient.width <= 0 || gradient.height <= 0) {
                return skin->title_gradient_at(t, t);
            }

            return gradient.GetPixel((int32_t)(t * (gradient.width - 1)), gradient.height / 2);
        };

        olc::Pixel atmosphere = planet_color(0.2);
        atmosphere = olc::Pixel(
            (uint8_t)std::lerp((double)atmosphere.r, 255.0, 0.3),
            (uint8_t)std::lerp((double)atmosphere.g, 255.0, 0.3),
            (uint8_t)std::lerp((double)atmosphere.b, 255.0, 0.3)
        );

        for (int32_t y = 0; y < STARFIELD_PLANET_SPRITE_SIZE; y++) {
            for (int32_t x = 0; x < STARFIELD_PLANET_SPRITE_SIZE; x++) {
                double dx = (x + 0.5 - half) / body_radius;
                double dy = (y + 0.5 - half) / body_radius;
                double r = std::sqrt(dx * dx + dy * dy);

                double facing = r > 0 ? (dx * light.x + dy * light.y) / (r * light_flat) : 0;
                double rim_weight = 0.1 + 0.9 * std::pow(0.5 + 0.5 * facing, 2.0);
                double rim_distance = r - 1;
                double rim = std::exp(-std::pow(rim_distance / (rim_distance < 0 ? 0.05 : 0.1), 2.0));
                halo_sprite->SetPixel(x, y, olc::Pixel(atmosphere.r, atmosphere.g, atmosphere.b, alpha_of(rim * rim_weight * 0.35)));

                double coverage = std::clamp((1 - r) * body_radius + 0.5, 0.0, 1.0);

                if (coverage <= 0) {
                    body_sprite->SetPixel(x, y, olc::BLANK);
                    continue;
                }

                double clamped = std::min(r, 1.0);
                double nx = clamped > 0 ? dx * clamped / r : 0;
                double ny = clamped > 0 ? dy * clamped / r : 0;
                double nz = std::sqrt(std::max(0.0, 1 - clamped * clamped));

                double latitude = std::asin(std::clamp(nx * pole.x + ny * pole.y + nz * pole.z, -1.0, 1.0));
                latitude += 0.12 * (value_noise(nx * 4 + 11, ny * 4 + 7, seed) - 0.5);
                latitude += 0.05 * (value_noise(nx * 12 + 3, ny * 12 + 19, seed + 1) - 0.5);

                double band = 0.5 + 0.35 * std::sin(latitude * 4) + 0.12 * std::sin(latitude * 9 + 1.3) + 0.03 * std::sin(latitude * 21 + 0.4);
                olc::Pixel color = planet_color(band);

                double diffuse = nx * light.x + ny * light.y + nz * light.z;
                double lit = std::pow(std::clamp((diffuse + 0.08) / 1.08, 0.0, 1.0), 1.2);
                double shade = 0.02 + 0.4 * lit * (0.7 + 0.3 * nz);

                body_sprite->SetPixel(x, y, olc::Pixel((uint8_t)(color.r * shade), (uint8_t)(color.g * shade), (uint8_t)(color.b * shade), alpha_of(coverage)));
            }
        }

        planet_body = SpriteDecal(std::move(body_sprite), true);
        planet_halo = SpriteDecal(std::move(halo_sprite), true);
    }

    void EntityStarfield::update_camera() {
        double angle = -time * STARFIELD_ROTATION_SPEED;
        double c = std::cos(angle);
        double s = std::sin(angle);
        double t = 1 - c;
        const Direction& k = rotation_axis;

        camera = {
            c + k.x * k.x * t, k.x * k.y * t - k.z * s, k.x * k.z * t + k.y * s,
            k.y * k.x * t + k.z * s, c + k.y * k.y * t, k.y * k.z * t - k.x * s,
            k.z * k.x * t - k.y * s, k.z * k.y * t + k.x * s, c + k.z * k.z * t
        };
    }

    void EntityStarfield::update_planet() {
        olc::vd2d reference;
        double depth;

        if (project(rotation_axis, planet_screen, depth) && project(planet_reference, reference, depth)) {
            planet_angle = std::atan2(reference.y - planet_screen.y, reference.x - planet_screen.x) - planet_initial_angle;
        }
    }

    bool EntityStarfield::project(const Direction& direction, olc::vd2d& screen, double& depth) const {
        const std::array<double, 9>& m = camera;
        double x = m[0] * direction.x + m[1] * direction.y + m[2] * direction.z;
        double y = m[3] * direction.x + m[4] * direction.y + m[5] * direction.z;
        double z = m[6] * direction.x + m[7] * direction.y + m[8] * direction.z;

        if (z <= 0.15) {
            return false;
        }

        screen = {size.x / 2 + x / z * focal, size.y / 2 - y / z * focal};
        depth = z;
        return true;
    }

    void EntityStarfield::init(Scene*) {
        focal = (size.x / 2) / std::tan(STARFIELD_FOV / 2);
        rotation_axis = normalized((STARFIELD_PLANET_X - 0.5) * size.x / focal, -(STARFIELD_PLANET_Y - 0.5) * size.y / focal, 1);
        planet_reference = normalized(rotation_axis.x, rotation_axis.y + 0.05, rotation_axis.z);

        build_sky();
        build_sprites();
        build_planet();
        update_camera();

        planet_initial_angle = 0;
        update_planet();
        planet_initial_angle = planet_angle;
        planet_angle = 0;

        shooting_timer = random(3.0, STARFIELD_SHOOTING_MIN_DELAY);
    }

    void EntityStarfield::update(Scene*, float elapsed_time) {
        time += elapsed_time;
        update_camera();
        update_planet();

        for (ShootingStar& shooting : shooting_stars) {
            shooting.age += elapsed_time;
            shooting.pos += shooting.velocity * (double)elapsed_time;
        }

        std::erase_if(shooting_stars, [](const ShootingStar& shooting) { return shooting.age >= shooting.life; });

        shooting_timer -= elapsed_time;

        if (shooting_timer <= 0) {
            spawn_shooting_star();
            shooting_timer = random(STARFIELD_SHOOTING_MIN_DELAY, STARFIELD_SHOOTING_MAX_DELAY);
        }
    }

    void EntityStarfield::draw_sprite(olc::PixelGameEngine* window, const SpriteDecal& sprite, olc::vd2d center, double size, olc::Pixel tint) const {
        if (center.x < -size || center.y < -size || center.x > this->size.x + size || center.y > this->size.y + size) {
            return;
        }

        float scale = (float)(size / STARFIELD_SPRITE_SIZE);
        window->DrawDecal({(float)(center.x - size / 2), (float)(center.y - size / 2)}, sprite.get(), {scale, scale}, tint);
    }

    void EntityStarfield::draw_star(olc::PixelGameEngine* window, const Star& star) const {
        olc::vd2d screen;
        double depth;

        if (!project(star.direction, screen, depth)) {
            return;
        }

        double twinkle = 0.5 + 0.5 * std::sin(time * star.twinkle_rate + star.phase);
        double brightness = (0.12 + 0.6 * star.brightness) * (1 - 0.45 * twinkle * (1.1 - star.brightness));
        double star_size = std::lerp(STARFIELD_MIN_STAR_SIZE, STARFIELD_MAX_STAR_SIZE, star.brightness);

        olc::Pixel color = star.tint;
        color.a = alpha_of(brightness);
        draw_sprite(window, glow, screen, star_size, color);

        if (star.brightness > STARFIELD_FLARE_BRIGHTNESS) {
            color.a = alpha_of((0.15 + 0.45 * twinkle) * brightness);
            draw_sprite(window, flare, screen, star_size * 2.4, color);
        }
    }

    void EntityStarfield::draw_shooting_star(olc::PixelGameEngine* window, const ShootingStar& shooting) const {
        double envelope = std::sin(3.14159265 * std::clamp(shooting.age / shooting.life, 0.0, 1.0));
        olc::vd2d direction = shooting.velocity.norm();
        olc::vd2d side = olc::vd2d{-direction.y, direction.x} * 1.2;
        olc::vd2d tail = shooting.pos - direction * shooting.length;

        olc::Pixel head_color = shooting.tint;
        head_color.a = alpha_of(envelope * 0.65);
        olc::Pixel tail_color = shooting.tint;
        tail_color.a = 0;

        window->GradientTriangleDecal(shooting.pos + side, shooting.pos - side, tail, head_color, head_color, tail_color);

        olc::Pixel glow_color = olc::WHITE;
        glow_color.a = alpha_of(envelope * 0.75);
        draw_sprite(window, glow, shooting.pos, 14, glow_color);
    }

    void EntityStarfield::draw_planet(olc::PixelGameEngine* window) const {
        float half = STARFIELD_PLANET_SPRITE_SIZE / 2.0f;
        float scale = (float)(STARFIELD_PLANET_RADIUS / (STARFIELD_PLANET_SPRITE_SIZE * STARFIELD_PLANET_BODY_RATIO));
        olc::vf2d position = {(float)planet_screen.x, (float)planet_screen.y};
        olc::vf2d source = {(float)STARFIELD_PLANET_SPRITE_SIZE, (float)STARFIELD_PLANET_SPRITE_SIZE};

        window->SetDecalMode(olc::DecalMode::NORMAL);
        window->DrawPartialRotatedDecal(position, planet_body.get(), (float)planet_angle, {half, half}, {0, 0}, source, {scale, scale});

        window->SetDecalMode(olc::DecalMode::ADDITIVE);
        window->DrawPartialRotatedDecal(position, planet_halo.get(), (float)planet_angle, {half, half}, {0, 0}, source, {scale, scale});
    }

    void EntityStarfield::draw(olc::PixelGameEngine* window) {
        if (!glow || !flare || !planet_body || !planet_halo) {
            return;
        }

        window->SetDecalMode(olc::DecalMode::ADDITIVE);

        for (const Star& star : stars) {
            draw_star(window, star);
        }

        for (const ShootingStar& shooting : shooting_stars) {
            draw_shooting_star(window, shooting);
        }

        draw_planet(window);

        window->SetDecalMode(olc::DecalMode::NORMAL);
    }
}
