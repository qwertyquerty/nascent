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

    void SceneInput::start_capture(bool all) {
        capture_all = all;

        if (capture_all) {
            selected_lane = 0;
        }

        capturing = true;
        capture_time = now_ms();
        status.clear();
        play_menu_sound();
    }

    void SceneInput::change_mode(int32_t direction) {
        uint8_t next = (uint8_t)std::clamp((int32_t)key_count + direction, (int32_t)MIN_KEY_COUNT, (int32_t)MAX_KEY_COUNT);

        if (next == key_count) {
            return;
        }

        load_mode(next);
        status.clear();
        play_menu_sound();
    }

    bool SceneInput::update_mouse(Input& input) {
        if (int32_t wheel = input.take_wheel(); wheel != 0) {
            change_mode(wheel > 0 ? 1 : -1);
            return true;
        }

        std::optional<int32_t> clicked = spots.take_click(input);

        if (!clicked) {
            return false;
        }

        switch (*clicked) {
            case SPOT_MODE_DOWN:
                change_mode(-1);
                break;
            case SPOT_MODE_UP:
                change_mode(1);
                break;
            case SPOT_REBIND:
                start_capture(false);
                break;
            case SPOT_REBIND_ALL:
                start_capture(true);
                break;
            case SPOT_RESET:
                reset();
                break;
            case SPOT_BACK:
                input.inject(actions::BACK);
                return false;
            default:
                selected_lane = (uint8_t)(*clicked - SPOT_LANE);
                start_capture(false);
                break;
        }

        return true;
    }

    void SceneInput::update(float elapsed_time) {
        Input& input = game->input();
        timer += elapsed_time;

        if (!capturing && update_mouse(input)) {
            return;
        }

        if (capturing && spots.take_click(input) == SPOT_BACK) {
            input.inject(actions::BACK);
        }

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
            change_mode(1);
        }
        else if (key_count > MIN_KEY_COUNT && input.take(actions::DOWN)) {
            change_mode(-1);
        }
        else if (input.take(actions::CONFIRM)) {
            start_capture(input.key_held(InputManager::keybind_mod1));
        }
        else if (InputManager::reset_pressed(input)) {
            reset();
        }
    }

    void SceneInput::draw_lanes(olc::PixelGameEngine* window, int32_t top) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t margin = (int32_t)(screen.x * Ui::layout("input.left"));

        float gap_ratio = (float)Ui::layout("input.lane_gap_ratio");
        float available = (float)(screen.x - margin * 2);
        float size = std::min(Ui::layout_pxf("input.max_lane_size"), available / (key_count + (key_count - 1) * gap_ratio));
        float gap = size * gap_ratio;
        float total = size * key_count + gap * (key_count - 1);
        float left = (screen.x - total) / 2;

        uint32_t held = InputManager::get_lane_keys_held(game->input(), lane_keys);
        olc::FontRect metrics = text_bounds(skin->display_font_medium, "Hgjy");
        uint8_t held_alpha = (uint8_t)Ui::layout("input.held_note_alpha");
        uint8_t selected_alpha = (uint8_t)Ui::layout("input.selected_note_alpha");
        uint8_t idle_alpha = (uint8_t)Ui::layout("input.idle_note_alpha");
        float label_gap = Ui::layout_pxf("input.label_gap");
        float label_padding = Ui::layout_pxf("input.label_padding");
        float label_fit_margin = Ui::layout_pxf("input.label_fit_margin");
        double hover_mix = Ui::layout("input.lane_hover_mix");
        float underline = std::max(1.0f, Ui::layout_pxf("input.underline_thickness"));
        float number_offset = Ui::layout_pxf("input.lane_number_offset");
        double pulse_low = Ui::layout("input.capture_pulse_min_alpha");
        double pulse_high = Ui::layout("input.capture_pulse_max_alpha");
        float pulse_speed = (float)Ui::layout("input.capture_pulse_speed");

        for (uint8_t lane = 0; lane < key_count; lane++) {
            float lane_x = left + lane * (size + gap);
            bool selected = lane == selected_lane;
            bool waiting = capturing && selected;

            olc::Pixel color = skin->title_gradient_at(key_count > 1 ? (double)lane / (key_count - 1) : 0.5, 0.5);
            color.a = (held >> lane) & 1 ? held_alpha : selected ? selected_alpha
                                                                 : idle_alpha;

            if (const MipDecal* note = skin->image(SkinImage::NOTE, lane, key_count)) {
                float scale = size / note->size().x;
                MipDecal::Pick level = note->pick(scale);
                window->DrawDecal({lane_x, (float)top}, level.decal, {scale / level.factor, scale / level.factor}, color);
            }

            float label_top = top + size + label_gap;
            Rect lane_area = {{lane_x, (float)top}, {size, label_top + (float)metrics.size.y + label_padding * 2 - top}};
            spots.add(lane_area, SPOT_LANE + lane);
            olc::Pixel lane_background = skin->color(SkinColor::INPUT_LANE_BACKGROUND);
            window->FillRectDecal({lane_x, label_top}, {size, (float)metrics.size.y + label_padding * 2}, !capturing && lane_area.contains(window->GetMousePos()) ? mix(lane_background, skin->color(SkinColor::TEXT), hover_mix) : lane_background);

            std::string label = waiting ? "?" : InputManager::key_label(lane_keys[lane]);
            olc::Font* font = &skin->display_font_medium;

            if (text_bounds(*font, label).size.x > size - label_fit_margin) {
                font = &skin->display_font_small;
            }

            olc::FontRect bounds = text_bounds(*font, label);
            olc::Pixel label_color = skin->color(selected ? SkinColor::TEXT : SkinColor::TEXT_MUTED);

            if (waiting) {
                label_color = fade(label_color, (pulse_low + (pulse_high - pulse_low) * (0.5 + 0.5 * std::sin(timer * pulse_speed))) / 255.0);
            }

            int32_t label_x = (int32_t)std::lround(lane_x + size / 2 - bounds.size.x / 2.0f - bounds.offset.x);
            int32_t label_y = (int32_t)std::lround(label_top + label_padding + (metrics.size.y - bounds.size.y) / 2.0f - bounds.offset.y);
            draw_text(*font, label, {label_x, label_y}, label_color);

            if (selected) {
                olc::Pixel underline_color = skin->title_gradient_at(key_count > 1 ? (double)lane / (key_count - 1) : 0.5, 0.5);
                window->FillRectDecal({lane_x, label_top + metrics.size.y + label_padding * 2}, {size, underline}, underline_color);
            }

            std::string number = std::format("{}", lane + 1);
            olc::FontRect number_bounds = text_bounds(skin->display_font_small, number);
            int32_t number_x = (int32_t)std::lround(lane_x + size / 2 - number_bounds.size.x / 2.0f - number_bounds.offset.x);
            draw_text(skin->display_font_small, number, {number_x, (int32_t)(label_top + metrics.size.y + number_offset)}, skin->color(SkinColor::TEXT_FAINT));
        }
    }

    void SceneInput::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = (int32_t)(screen.x * Ui::layout("input.left"));
        int32_t y = (int32_t)(screen.y * Ui::layout("input.top"));

        draw_text(skin->display_font_large, "Input", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::layout_px("input.subtitle_offset");

        draw_text(skin->display_font_small, "Lane keys used while playing. Changes are saved as you make them.", {x, y}, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::layout_px("input.key_mode_offset");

        spots.clear();
        int32_t mode_x = x + Ui::layout_px("input.key_mode_value_x");
        float button_gap = Ui::layout_pxf("input.button_gap");
        draw_text(skin->display_font_medium, "Key mode", {x, y}, skin->color(SkinColor::TEXT_MUTED));
        draw_text(skin->display_font_medium, std::format("{}K", key_count), {mode_x, y}, skin->color(SkinColor::TEXT));
        float mode_buttons = (float)mode_x + text_bounds(skin->display_font_medium, "10K").size.x + button_gap;
        draw_hints(window, skin->display_font_medium, {{"-", SPOT_MODE_DOWN}, {"+", SPOT_MODE_UP}}, {mode_buttons, (float)y}, button_gap, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);

        bool custom = InputManager::is_custom_lane_keys(key_count);
        draw_text(skin->display_font_small, custom ? "Custom layout" : "Default layout", {x + Ui::layout_px("input.layout_label_x"), y}, skin->color(custom ? SkinColor::ATTENTION : SkinColor::TEXT_FAINT));

        draw_lanes(window, (int32_t)(screen.y * Ui::layout("input.lanes_top")));

        std::string line = status;
        olc::Pixel line_color = skin->color(status_is_error ? SkinColor::TEXT_ERROR : SkinColor::TEXT_MUTED);

        if (capturing) {
            line = capture_all ? std::format("Press a key for lane {} of {}", selected_lane + 1, key_count) : std::format("Press a key for lane {}", selected_lane + 1);
            line_color = skin->color(SkinColor::TEXT);
        }

        if (!line.empty()) {
            olc::FontRect bounds = text_bounds(skin->display_font_small, line);
            draw_text(skin->display_font_small, line, {(screen.x - bounds.size.x) / 2 - bounds.offset.x, (int32_t)(screen.y * Ui::layout("input.status_top"))}, line_color);
        }

        std::vector<Hint> footer = capturing ? std::vector<Hint>{{"Press any key to bind it"}, {"Esc:  back", SPOT_BACK}}
                                             : std::vector<Hint>{{"←/→:  lane"}, {"↑/↓:  key mode"}, {"Enter:  rebind", SPOT_REBIND}, {"Shift+Enter:  rebind all", SPOT_REBIND_ALL}, {"Ctrl+R:  reset", SPOT_RESET}, {"Esc:  back", SPOT_BACK}};
        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, "Hgjy");
        int32_t footer_bottom = screen.y - (int32_t)(screen.y * Ui::layout("input.footer_bottom"));
        draw_hints(window, skin->display_font_small, footer, {(float)x, (float)(footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y))}, Ui::layout_pxf("input.footer_hint_gap"), skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);
    }
}
