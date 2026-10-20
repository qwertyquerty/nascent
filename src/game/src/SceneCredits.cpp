#include "SceneCredits.h"

#include "Skin.h"
#include "StatusBar.h"
#include "gse/Game.h"
#include "gse/render/Color.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <random>

namespace nascent {
    static std::string spaced_caps(std::string_view text) {
        std::string spaced;

        for (char c : text) {
            if (!spaced.empty()) {
                spaced += c == ' ' ? "  " : " ";
            }

            if (c != ' ') {
                spaced += (char)std::toupper((unsigned char)c);
            }
        }

        return spaced;
    }

    std::vector<CreditsSection> parse_credits(const nlohmann::json& credits) {
        std::vector<CreditsSection> sections;

        if (!credits.is_object() || !credits.contains("sections") || !credits["sections"].is_array()) {
            return sections;
        }

        for (const nlohmann::json& entry : credits["sections"]) {
            if (!entry.is_object()) {
                continue;
            }

            CreditsSection section;
            section.heading = entry.contains("heading") && entry["heading"].is_string() ? entry["heading"].get<std::string>() : std::string();

            if (entry.contains("names") && entry["names"].is_array()) {
                for (const nlohmann::json& name : entry["names"]) {
                    if (name.is_string()) {
                        section.names.push_back(name.get<std::string>());
                    }
                }
            }

            if (!section.names.empty()) {
                sections.push_back(std::move(section));
            }
        }

        return sections;
    }

    SceneCredits::SceneCredits(Skin* skin) : skin(skin) {}

    void SceneCredits::on_create() {
        std::ifstream file(CREDITS_PATH, std::ios::binary);
        sections = parse_credits(nlohmann::json::parse(file, nullptr, false));

        if (sections.empty()) {
            sections.push_back({"", {"The credits couldn't be loaded"}});
        }

        logo = MipDecal(MipDecal::build_levels(std::make_unique<olc::Sprite>(LOGO_PATH)));
        std::filesystem::path arrows_folder = Skin::skin_directory(Skin::DEFAULT_SKIN);
        std::error_code ec;

        for (size_t index = 1; index <= MAX_ARROW_IMAGES; index++) {
            std::filesystem::path path = arrows_folder / std::format("note_{}.png", index);

            if (!std::filesystem::exists(path, ec)) {
                break;
            }

            arrow_images.emplace_back(MipDecal::build_levels(std::make_unique<olc::Sprite>(path.string())));
        }

        on_resize();
    }

    void SceneCredits::on_resize() {
        olc::vi2d screen = game->window->GetScreenSize();
        std::mt19937 random(ARROW_SEED);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        float min_size = (float)Ui::layout("credits.arrow_min_size");
        float max_size = (float)Ui::layout("credits.arrow_max_size");
        float min_speed = (float)Ui::layout("credits.arrow_min_speed");
        float max_speed = (float)Ui::layout("credits.arrow_max_speed");
        arrows.clear();

        for (int32_t index = 0; index < ARROW_COUNT; index++) {
            float size = Ui::pxf(min_size + (max_size - min_size) * unit(random));
            float speed = Ui::pxf(min_speed + (max_speed - min_speed) * unit(random));
            float x = unit(random) * (float)screen.x;
            float y = unit(random) * (float)screen.y;
            arrows.push_back({x, y, size, speed, unit(random), (size_t)random()});
        }

        if (!scroll_started) {
            scroll = (float)screen.y * (float)Ui::layout("credits.start_fraction");
        }
    }

    void SceneCredits::update_arrows(float elapsed_time, olc::vi2d screen) {
        for (Arrow& arrow : arrows) {
            arrow.y -= arrow.speed * elapsed_time;

            if (arrow.y < -arrow.size) {
                arrow.y += (float)screen.y + arrow.size * 2.0f;
            }
        }
    }

    void SceneCredits::update(float elapsed_time) {
        Input& input = game->input();
        olc::vi2d screen = game->window->GetScreenSize();
        scroll_started = true;

        if (spots.take_click(input)) {
            input.inject(actions::BACK);
        }

        if (input.take(actions::BACK)) {
            game->audio().play_effect(skin->get_random_menu_pluck());
            game->scenes().pop();
            return;
        }

        scroll += Ui::layout_pxf("credits.scroll_speed") * elapsed_time;
        float loop = (float)screen.y + content_height;

        if (content_height > 0 && scroll > loop) {
            scroll -= loop;
        }

        update_arrows(elapsed_time, screen);
    }

    void SceneCredits::draw_backdrop(olc::PixelGameEngine* window, olc::vi2d screen) const {
        window->FillRectDecal({0.0f, 0.0f}, {(float)screen.x, (float)screen.y}, skin->color(SkinColor::SCREEN_BACKGROUND));

        if (arrow_images.empty()) {
            return;
        }

        uint8_t arrow_alpha = (uint8_t)Ui::layout("credits.arrow_alpha");

        for (const Arrow& arrow : arrows) {
            const MipDecal& image = arrow_images[arrow.image % arrow_images.size()];

            if (!image || image.size().x <= 0) {
                continue;
            }

            olc::Pixel color = skin->title_gradient_at(arrow.hue, 0.5);
            color.a = arrow_alpha;
            float scale = arrow.size / image.size().x;
            MipDecal::Pick level = image.pick(scale);
            window->DrawDecal({arrow.x - arrow.size / 2.0f, arrow.y}, level.decal, {scale / level.factor, scale / level.factor}, color);
        }
    }

    std::optional<float> SceneCredits::line_opacity(float baseline, olc::vi2d screen) const {
        float edge = (float)screen.y * (float)Ui::layout("credits.edge_fade_fraction");
        float top = (float)StatusBar::height();
        float bottom = (float)(screen.y - StatusBar::height());
        float fade_in = std::clamp(std::min(baseline - top, bottom - baseline) / edge, 0.0f, 1.0f);

        if (fade_in <= 0.0f) {
            return std::nullopt;
        }

        return fade_in * fade_in * (3.0f - 2.0f * fade_in);
    }

    void SceneCredits::draw_line(olc::Font& font, const std::string& text, float baseline, olc::vi2d screen, olc::Pixel color, bool glow) const {
        std::optional<float> opacity = line_opacity(baseline, screen);

        if (!opacity) {
            return;
        }

        float x = (float)screen.x / 2.0f;

        if (glow) {
            for (olc::vf2d offset : Skin::glow_offsets(std::max(1.0f, Ui::pxf(Skin::GRADE_GLOW_RADIUS)))) {
                draw_text_centered(font, text, x + offset.x, baseline + offset.y, Skin::glow_color(color, *opacity));
            }
        }

        draw_text_centered(font, text, x, baseline, fade(color, *opacity));
    }

    void SceneCredits::draw_logo(olc::PixelGameEngine* window, float baseline, olc::vi2d screen) const {
        std::optional<float> opacity = line_opacity(baseline, screen);

        if (!opacity || logo.size().x <= 0) {
            return;
        }

        float scale = Ui::layout_pxf("credits.logo_width") / logo.size().x;
        olc::vf2d drawn = logo.size() * scale;
        MipDecal::Pick level = logo.pick(scale);
        window->DrawDecal({((float)screen.x - drawn.x) / 2.0f, baseline - drawn.y}, level.decal, {scale / level.factor, scale / level.factor}, fade(olc::WHITE, *opacity));
    }

    void SceneCredits::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        draw_backdrop(window, screen);

        auto gradient = [&](float position) {
            return skin->title_gradient_at(std::fmod(position, 1.0f), 0.5);
        };

        float lead_gap = Ui::layout_pxf("credits.lead_gap");
        float name_gap = Ui::layout_pxf("credits.name_gap");
        float heading_gap = Ui::layout_pxf("credits.heading_gap");
        float section_gap = Ui::layout_pxf("credits.section_gap");
        float name_gradient_step = (float)Ui::layout("credits.name_gradient_step");
        float start = (float)screen.y + lead_gap - scroll;
        float y = start;
        if (logo) {
            draw_logo(window, y, screen);
        }
        else {
            draw_line(skin->display_font_large, TITLE, y, screen, gradient(0.0f), true);
        }

        y += Ui::layout_pxf("credits.title_gap");
        size_t name_index = 0;

        for (const CreditsSection& section : sections) {
            if (!section.heading.empty()) {
                draw_line(skin->display_font_small, spaced_caps(section.heading), y, screen, skin->color(SkinColor::TEXT_MUTED), false);
                y += heading_gap;
            }

            bool lead = section.names.size() == 1;

            for (const std::string& name : section.names) {
                draw_line(skin->display_font_medium, name, y, screen, gradient((float)name_index * name_gradient_step), lead);
                y += lead ? lead_gap : name_gap;
                name_index++;
            }

            y += section_gap;
        }

        draw_line(skin->display_font_medium, CLOSING, y, screen, skin->color(SkinColor::TEXT), false);
        content_height = y - start + lead_gap;

        olc::Font& font = skin->display_font_small;
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t padding = StatusBar::padding();
        float baseline = (float)(screen.y - StatusBar::height() + (StatusBar::height() - metrics.size.y) / 2 - metrics.offset.y);
        std::vector<Hint> hints = {{BACK_HINT, 0}};
        float hint_gap = Ui::layout_pxf("credits.hint_gap");
        float right = (float)(screen.x - padding) - hints_width(font, hints, hint_gap);
        spots.clear();
        draw_hints(window, font, hints, {right, baseline}, hint_gap, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);
    }
}
