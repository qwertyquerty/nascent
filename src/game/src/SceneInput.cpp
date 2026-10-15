#include "SceneInput.h"
#include "RichPresence.h"

#include "gse/Game.h"
#include "InputManager.h"
#include "gse/input/KeyEvents.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "gse/render/Color.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace nascent {
    static constexpr float MAX_LANE_SIZE = 150.0f;
    static constexpr float LANE_GAP_RATIO = 0.18f;
    static constexpr uint8_t IDLE_NOTE_ALPHA = 70;

    SceneInput::SceneInput(Skin* skin, uint8_t key_count) {
        this->skin = skin;
        this->key_count = std::clamp(key_count, MIN_KEY_COUNT, MAX_KEY_COUNT);
    }

    void SceneInput::on_create() {
        load_mode(key_count);
    }

    void SceneInput::on_enter() {
        RichPresence::in_menu("Changing keybinds");
        capturing = false;
        capture_all = false;
        status.clear();
    }

    void SceneInput::play_menu_sound() {
        game->audio().play_effect(skin->get_random_menu_pluck());
    }

    void SceneInput::load_mode(uint8_t key_count) {
        this->key_count = key_count;
        lane_keys = InputManager::get_lane_keys(key_count);
        selected_lane = std::min<uint8_t>(selected_lane, key_count - 1);
        capturing = false;
        capture_all = false;
    }

    void SceneInput::bind(olc::Key key) {
        uint8_t lane = selected_lane;
        auto existing = std::find(lane_keys.begin(), lane_keys.end(), key);
        size_t existing_lane = (size_t)(existing - lane_keys.begin());

        if (existing != lane_keys.end() && existing_lane != lane) {
            *existing = lane_keys[lane];
            status = std::format("Lane {} set to {}, swapped with lane {}", lane + 1, InputManager::key_label(key), existing_lane + 1);
        }
        else {
            status = std::format("Lane {} set to {}", lane + 1, InputManager::key_label(key));
        }

        lane_keys[lane] = key;
        status_is_error = false;

        if (!InputManager::set_lane_keys(key_count, lane_keys)) {
            status = "Could not save settings";
            status_is_error = true;
        }

        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));

        if (capture_all && lane + 1 < key_count) {
            selected_lane++;
            return;
        }

        capturing = false;
        capture_all = false;
    }

    void SceneInput::reset() {
        if (!InputManager::reset_lane_keys(key_count)) {
            status = "Could not save settings";
            status_is_error = true;
        }
        else {
            status = std::format("{}K keys reset to the defaults", key_count);
            status_is_error = false;
        }

        load_mode(key_count);
        play_menu_sound();
    }

    void SceneInput::update(float elapsed_time) {
        Input& input = game->input();
        timer += elapsed_time;

        if (capturing) {
            for (const KeyEvent& event : game->input().events()) {
                if (event.pressed && event.time >= capture_time && event.key != olc::Key::NONE && event.key != InputManager::back_key && event.key != InputManager::exit_key) {
                    bind(event.key);
                    break;
                }
            }

            return;
        }

        if (input.take(actions::LEFT)) {
            selected_lane = (uint8_t)((selected_lane + key_count - 1) % key_count);
            play_menu_sound();
        }
        else if (input.take(actions::RIGHT)) {
            selected_lane = (uint8_t)((selected_lane + 1) % key_count);
            play_menu_sound();
        }
        else if (key_count < MAX_KEY_COUNT && input.take(actions::UP)) {
            load_mode((uint8_t)(key_count + 1));
            status.clear();
            play_menu_sound();
        }
        else if (key_count > MIN_KEY_COUNT && input.take(actions::DOWN)) {
            load_mode((uint8_t)(key_count - 1));
            status.clear();
            play_menu_sound();
        }
        else if (input.take(actions::CONFIRM)) {
            capture_all = input.key_held(InputManager::keybind_mod1);

            if (capture_all) {
                selected_lane = 0;
            }

            capturing = true;
            capture_time = now_ms();
            status.clear();
            play_menu_sound();
        }
        else if (InputManager::reset_pressed(input)) {
            reset();
        }
    }

    void SceneInput::draw_lanes(olc::PixelGameEngine* window, int32_t top) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t margin = screen.x / 24;

        float available = (float)(screen.x - margin * 2);
        float size = std::min(Ui::pxf(MAX_LANE_SIZE), available / (key_count + (key_count - 1) * LANE_GAP_RATIO));
        float gap = size * LANE_GAP_RATIO;
        float total = size * key_count + gap * (key_count - 1);
        float left = (screen.x - total) / 2;

        uint32_t held = InputManager::get_lane_keys_held(game->input(), lane_keys);
        olc::FontRect metrics = text_bounds(skin->display_font_medium, "Hgjy");

        for (uint8_t lane = 0; lane < key_count; lane++) {
            float lane_x = left + lane * (size + gap);
            bool selected = lane == selected_lane;
            bool waiting = capturing && selected;

            olc::Pixel color = skin->title_gradient_at(key_count > 1 ? (double)lane / (key_count - 1) : 0.5, 0.5);
            color.a = (held >> lane) & 1 ? 255 : selected ? 170
                                                          : IDLE_NOTE_ALPHA;

            if (const MipDecal* note = skin->image(SkinImage::NOTE, lane, key_count)) {
                float scale = size / note->size().x;
                MipDecal::Pick level = note->pick(scale);
                window->DrawDecal({lane_x, (float)top}, level.decal, {scale / level.factor, scale / level.factor}, color);
            }

            float label_top = top + size + Ui::pxf(16);
            window->FillRectDecal({lane_x, label_top}, {size, (float)metrics.size.y + Ui::pxf(24)}, skin->color(SkinColor::INPUT_LANE_BACKGROUND));

            std::string label = waiting ? "?" : InputManager::key_label(lane_keys[lane]);
            olc::Font* font = &skin->display_font_medium;

            if (text_bounds(*font, label).size.x > size - Ui::pxf(12)) {
                font = &skin->display_font_small;
            }

            olc::FontRect bounds = text_bounds(*font, label);
            olc::Pixel label_color = skin->color(selected ? SkinColor::TEXT : SkinColor::TEXT_MUTED);

            if (waiting) {
                label_color = fade(label_color, (140 + 115 * (0.5 + 0.5 * std::sin(timer * 6))) / 255.0);
            }

            int32_t label_x = (int32_t)std::lround(lane_x + size / 2 - bounds.size.x / 2.0f - bounds.offset.x);
            int32_t label_y = (int32_t)std::lround(label_top + Ui::pxf(12) + (metrics.size.y - bounds.size.y) / 2.0f - bounds.offset.y);
            draw_text(*font, label, {label_x, label_y}, label_color);

            if (selected) {
                olc::Pixel underline = skin->title_gradient_at(key_count > 1 ? (double)lane / (key_count - 1) : 0.5, 0.5);
                window->FillRectDecal({lane_x, label_top + metrics.size.y + Ui::pxf(24)}, {size, std::max(1.0f, Ui::pxf(3))}, underline);
            }

            std::string number = std::format("{}", lane + 1);
            olc::FontRect number_bounds = text_bounds(skin->display_font_small, number);
            int32_t number_x = (int32_t)std::lround(lane_x + size / 2 - number_bounds.size.x / 2.0f - number_bounds.offset.x);
            draw_text(skin->display_font_small, number, {number_x, (int32_t)(label_top + metrics.size.y + Ui::pxf(52))}, skin->color(SkinColor::TEXT_FAINT));
        }
    }

    void SceneInput::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_large, "Input", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::px(90);

        draw_text(skin->display_font_small, "Lane keys used while playing. Changes are saved as you make them.", {x, y}, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::px(70);

        draw_text(skin->display_font_medium, "Key mode", {x, y}, skin->color(SkinColor::TEXT_MUTED));
        draw_text(skin->display_font_medium, std::format("{}K", key_count), {x + Ui::px(300), y}, skin->color(SkinColor::TEXT));

        bool custom = InputManager::is_custom_lane_keys(key_count);
        draw_text(skin->display_font_small, custom ? "Custom layout" : "Default layout", {x + Ui::px(400), y}, skin->color(custom ? SkinColor::ATTENTION : SkinColor::TEXT_FAINT));

        draw_lanes(window, (int32_t)(screen.y * 0.38));

        std::string line = status;
        olc::Pixel line_color = skin->color(status_is_error ? SkinColor::TEXT_ERROR : SkinColor::TEXT_MUTED);

        if (capturing) {
            line = capture_all ? std::format("Press a key for lane {} of {}", selected_lane + 1, key_count) : std::format("Press a key for lane {}", selected_lane + 1);
            line_color = skin->color(SkinColor::TEXT);
        }

        if (!line.empty()) {
            olc::FontRect bounds = text_bounds(skin->display_font_small, line);
            draw_text(skin->display_font_small, line, {(screen.x - bounds.size.x) / 2 - bounds.offset.x, (int32_t)(screen.y * 0.72)}, line_color);
        }

        std::string footer = capturing ? "Press any key to bind it        Esc:  back"
                                       : "←/→:  lane        ↑/↓:  key mode        Enter:  rebind        Shift+Enter:  rebind all        Ctrl+R:  reset        Esc:  back";

        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, footer);
        int32_t footer_bottom = screen.y - screen.y / 12;
        draw_text(skin->display_font_small, footer, {x, footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y)}, skin->color(SkinColor::TEXT_MUTED));
    }
}
