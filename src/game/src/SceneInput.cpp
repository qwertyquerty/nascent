#include "SceneInput.h"

#include "Game.h"
#include "InputManager.h"
#include "KeyEvents.h"
#include "Skin.h"
#include "TextCache.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace nascent {
    static const olc::Pixel LANE_BACKGROUND = olc::Pixel(20, 20, 24, 200);
    static const olc::Pixel CUSTOM_COLOR = olc::Pixel(240, 190, 70);
    static constexpr float MAX_LANE_SIZE = 150.0f;
    static constexpr float LANE_GAP_RATIO = 0.18f;
    static constexpr uint8_t IDLE_NOTE_ALPHA = 70;

    SceneInput::SceneInput(Skin* skin, Scene* return_scene, uint8_t key_count) {
        this->skin = skin;
        this->return_scene = return_scene;
        this->key_count = std::clamp(key_count, MIN_KEY_COUNT, MAX_KEY_COUNT);
    }

    void SceneInput::init(Game*) {
        load_mode(key_count);
    }

    void SceneInput::on_enter() {
        capturing = false;
        capture_all = false;
        status.clear();
    }

    void SceneInput::play_menu_sound() {
        game->play_sound(skin->get_random_menu_pluck());
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

        game->play_sound(skin->sound(SkinSound::HITSOUND));

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
        olc::PixelGameEngine* window = game->window;
        timer += elapsed_time;

        if (capturing) {
            for (const KeyEvent& event : game->get_frame_key_events()) {
                if (event.pressed && event.time >= capture_time && event.key != olc::Key::NONE && event.key != InputManager::back_key && event.key != InputManager::exit_key) {
                    bind(event.key);
                    break;
                }
            }

            return;
        }

        if (window->GetKey(InputManager::menu_left_key).bPressed) {
            selected_lane = (uint8_t)((selected_lane + key_count - 1) % key_count);
            play_menu_sound();
        }
        else if (window->GetKey(InputManager::menu_right_key).bPressed) {
            selected_lane = (uint8_t)((selected_lane + 1) % key_count);
            play_menu_sound();
        }
        else if (window->GetKey(InputManager::menu_up_key).bPressed && key_count < MAX_KEY_COUNT) {
            load_mode((uint8_t)(key_count + 1));
            status.clear();
            play_menu_sound();
        }
        else if (window->GetKey(InputManager::menu_down_key).bPressed && key_count > MIN_KEY_COUNT) {
            load_mode((uint8_t)(key_count - 1));
            status.clear();
            play_menu_sound();
        }
        else if (window->GetKey(InputManager::menu_start_key).bPressed) {
            capture_all = window->GetKey(InputManager::keybind_mod1).bHeld;

            if (capture_all) {
                selected_lane = 0;
            }

            capturing = true;
            capture_time = now_ms();
            status.clear();
            play_menu_sound();
        }
        else if (window->GetKey(InputManager::calibration_reset_key).bPressed) {
            reset();
        }
    }

    void SceneInput::draw_lanes(olc::PixelGameEngine* window, int32_t top) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t margin = screen.x / 24;

        float available = (float)(screen.x - margin * 2);
        float size = std::min(MAX_LANE_SIZE, available / (key_count + (key_count - 1) * LANE_GAP_RATIO));
        float gap = size * LANE_GAP_RATIO;
        float total = size * key_count + gap * (key_count - 1);
        float left = (screen.x - total) / 2;

        uint32_t held = InputManager::get_lane_keys_held(window, lane_keys);
        olc::FontRect metrics = text_bounds(skin->display_font_medium, "Hgjy");

        for (uint8_t lane = 0; lane < key_count; lane++) {
            float lane_x = left + lane * (size + gap);
            bool selected = lane == selected_lane;
            bool waiting = capturing && selected;

            olc::Pixel color = skin->title_gradient_at(key_count > 1 ? (double)lane / (key_count - 1) : 0.5, 0.5);
            color.a = (held >> lane) & 1 ? 255 : selected ? 170
                                                          : IDLE_NOTE_ALPHA;

            if (olc::Decal* note = skin->image(SkinImage::NOTE, lane, key_count)) {
                float scale = size / note->sprite->width;
                window->DrawDecal({lane_x, (float)top}, note, {scale, scale}, color);
            }

            float label_top = top + size + 16;
            window->FillRectDecal({lane_x, label_top}, {size, (float)metrics.size.y + 24}, LANE_BACKGROUND);

            std::string label = waiting ? "?" : InputManager::key_label(lane_keys[lane]);
            olc::Font* font = &skin->display_font_medium;

            if (text_bounds(*font, label).size.x > size - 12) {
                font = &skin->display_font_small;
            }

            olc::FontRect bounds = text_bounds(*font, label);
            olc::Pixel label_color = selected ? olc::WHITE : olc::GREY;

            if (waiting) {
                label_color.a = (uint8_t)(140 + 115 * (0.5 + 0.5 * std::sin(timer * 6)));
            }

            int32_t label_x = (int32_t)std::lround(lane_x + size / 2 - bounds.size.x / 2.0f - bounds.offset.x);
            int32_t label_y = (int32_t)std::lround(label_top + 12 + (metrics.size.y - bounds.size.y) / 2.0f - bounds.offset.y);
            draw_text(*font, label, {label_x, label_y}, label_color);

            if (selected) {
                olc::Pixel underline = skin->title_gradient_at(key_count > 1 ? (double)lane / (key_count - 1) : 0.5, 0.5);
                window->FillRectDecal({lane_x, label_top + metrics.size.y + 24}, {size, 3.0f}, underline);
            }

            std::string number = std::format("{}", lane + 1);
            olc::FontRect number_bounds = text_bounds(skin->display_font_small, number);
            int32_t number_x = (int32_t)std::lround(lane_x + size / 2 - number_bounds.size.x / 2.0f - number_bounds.offset.x);
            draw_text(skin->display_font_small, number, {number_x, (int32_t)(label_top + metrics.size.y + 52)}, olc::DARK_GREY);
        }
    }

    void SceneInput::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_large, "Input", {x, y}, olc::WHITE);
        y += 90;

        draw_text(skin->display_font_small, "Lane keys used while playing. Changes are saved as you make them.", {x, y}, olc::GREY);
        y += 70;

        draw_text(skin->display_font_medium, "Key mode", {x, y}, olc::GREY);
        draw_text(skin->display_font_medium, std::format("{}K", key_count), {x + 300, y}, olc::WHITE);

        bool custom = InputManager::is_custom_lane_keys(key_count);
        draw_text(skin->display_font_small, custom ? "Custom layout" : "Default layout", {x + 400, y}, custom ? CUSTOM_COLOR : olc::DARK_GREY);

        draw_lanes(window, (int32_t)(screen.y * 0.38));

        std::string line = status;
        olc::Pixel line_color = status_is_error ? olc::RED : olc::GREY;

        if (capturing) {
            line = capture_all ? std::format("Press a key for lane {} of {}", selected_lane + 1, key_count) : std::format("Press a key for lane {}", selected_lane + 1);
            line_color = olc::WHITE;
        }

        if (!line.empty()) {
            olc::FontRect bounds = text_bounds(skin->display_font_small, line);
            draw_text(skin->display_font_small, line, {(screen.x - bounds.size.x) / 2 - bounds.offset.x, (int32_t)(screen.y * 0.72)}, line_color);
        }

        std::string footer = capturing ? "Press any key to bind it        Esc:  back"
                                       : "←/→:  lane        ↑/↓:  key mode        Enter:  rebind        Shift+Enter:  rebind all        R:  reset        Esc:  back";

        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, footer);
        int32_t footer_bottom = screen.y - screen.y / 12;
        draw_text(skin->display_font_small, footer, {x, footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y)}, olc::GREY);
    }
}
