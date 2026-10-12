#include "SceneCalibration.h"
#include "Accuracy.h"
#include "Chart.h"
#include "EntityField.h"
#include "Game.h"
#include "InputManager.h"
#include "KeyEvents.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "Util.h"
#include "TextCache.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <numbers>

namespace nascent {
    static constexpr int32_t CLICK_BEAT_MS = 500;
    static constexpr int32_t CLICK_LOOP_BEATS = 16;
    static constexpr int32_t CLICK_LOOP_MS = CLICK_BEAT_MS * CLICK_LOOP_BEATS;
    static constexpr uint32_t CLICK_SAMPLE_RATE = 48000;
    static constexpr double CLICK_LENGTH_S = 0.04;

    static constexpr int32_t OFFSET_LIMIT_MS = 300;
    static constexpr int32_t SCROLL_SPEED_MIN = 20;
    static constexpr int32_t SCROLL_SPEED_MAX = 1000;
    static constexpr int32_t SCROLL_SPEED_STEP = 5;

    static constexpr size_t MAX_TAP_SAMPLES = 16;
    static constexpr size_t MIN_TAPS_FOR_SUGGESTION = 4;

    static void write_u16(std::ofstream& ofs, uint16_t value) {
        char bytes[2] = {(char)(value & 0xff), (char)((value >> 8) & 0xff)};
        ofs.write(bytes, 2);
    }

    static void write_u32(std::ofstream& ofs, uint32_t value) {
        char bytes[4] = {(char)(value & 0xff), (char)((value >> 8) & 0xff), (char)((value >> 16) & 0xff), (char)((value >> 24) & 0xff)};
        ofs.write(bytes, 4);
    }

    static bool write_click_track(const std::filesystem::path& path) {
        const uint32_t frames = CLICK_SAMPLE_RATE * CLICK_LOOP_MS / 1000;
        const uint32_t click_frames = (uint32_t)(CLICK_SAMPLE_RATE * CLICK_LENGTH_S);
        std::vector<int16_t> samples(frames, 0);

        for (int32_t beat = 0; beat < CLICK_LOOP_BEATS; beat++) {
            uint32_t start = (uint32_t)((int64_t)beat * CLICK_BEAT_MS * CLICK_SAMPLE_RATE / 1000);
            double frequency = beat % 4 == 0 ? 1760.0 : 1320.0;

            for (uint32_t i = 0; i < click_frames && start + i < frames; i++) {
                double t = (double)i / CLICK_SAMPLE_RATE;
                double envelope = std::exp(-t * 90.0);
                samples[start + i] = (int16_t)(std::sin(2.0 * std::numbers::pi * frequency * t) * envelope * 0.8 * 32767.0);
            }
        }

        std::ofstream ofs(path, std::ios::binary | std::ios::trunc);

        if (!ofs) {
            return false;
        }

        const uint32_t data_size = frames * sizeof(int16_t);

        ofs.write("RIFF", 4);
        write_u32(ofs, 36 + data_size);
        ofs.write("WAVE", 4);
        ofs.write("fmt ", 4);
        write_u32(ofs, 16);
        write_u16(ofs, 1);
        write_u16(ofs, 1);
        write_u32(ofs, CLICK_SAMPLE_RATE);
        write_u32(ofs, CLICK_SAMPLE_RATE * sizeof(int16_t));
        write_u16(ofs, sizeof(int16_t));
        write_u16(ofs, 16);
        ofs.write("data", 4);
        write_u32(ofs, data_size);

        for (int16_t sample : samples) {
            write_u16(ofs, (uint16_t)sample);
        }

        return (bool)ofs;
    }

    SceneCalibration::SceneCalibration(Skin* skin, Scene* return_scene) {
        this->skin = skin;
        this->return_scene = return_scene;
    }

    SceneCalibration::~SceneCalibration() {
    }

    void SceneCalibration::init(Game* game) {
        visual_offset = SettingsManager::settings.field_audio_visual_offset;
        input_offset = SettingsManager::settings.field_audio_input_offset;
        scroll_speed = SettingsManager::settings.field_scroll_speed;

        lane_keys = InputManager::get_lane_keys(4);
        tap_keys = lane_keys;
        tap_keys.push_back(olc::Key::SPACE);

        ChartInfo info;
        info.title = "Offset Calibration";
        info.key_count = 4;
        info.hit_accuracy = MAX_HIT_ACC_DIFFICULTY;

        std::vector<Hit> hits;

        for (int32_t beat = -4; beat < CLICK_LOOP_BEATS * 2; beat++) {
            hits.push_back({(uint8_t)positive_modulo(beat, 4), HitType::HIT, beat * CLICK_BEAT_MS, 0});
        }

        std::string error;
        chart = Chart::create(info, std::move(hits), {{0, (float)CLICK_BEAT_MS, 4}}, error);

        if (chart == nullptr) {
            std::cerr << "Calibration: " << error << std::endl;
            status_message = "Could not create the calibration chart";
            return;
        }

        olc::vi2d window_size = game->window->GetScreenSize();
        const uint8_t max_lanes_per_screen = 15;
        double lane_width = (double)window_size.x / max_lanes_per_screen;

        field = std::make_unique<EntityField>(chart.get(), skin, olc::vd2d{(double)window_size.x / 4 * 3 - (double)window_size.x / 24, 0.0}, olc::vd2d{(double)(window_size.x / 4), (double)window_size.y});
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = lane_width / 2;

        std::filesystem::path click_path = std::filesystem::temp_directory_path() / "nascent_calibration_click.wav";

        if (write_click_track(click_path)) {
            click_audio_id = game->get_audio().LoadSound(click_path.string());
            game->apply_effects_volume(click_audio_id);
            game->get_audio().Play(click_audio_id, true);
        }
        else {
            status_message = std::format("Could not write the click track to {}", path_utf8(click_path));
        }

        clock.max_error = SettingsManager::settings.field_audio_sync_max_error;
        clock.reset(0, now_ms());
    }

    void SceneCalibration::on_exit() {
        if (click_audio_id != -1) {
            game->get_audio().Stop(click_audio_id);
            game->get_audio().UnloadSound(click_audio_id);
            click_audio_id = -1;
        }
    }

    void SceneCalibration::adjust(int32_t direction, bool coarse) {
        switch (step) {
            case Step::VISUAL_OFFSET:
                visual_offset = std::clamp(visual_offset + direction * (coarse ? 10 : 1), -OFFSET_LIMIT_MS, OFFSET_LIMIT_MS);
                break;
            case Step::INPUT_OFFSET:
                input_offset = std::clamp(input_offset + direction * (coarse ? 10 : 1), -OFFSET_LIMIT_MS, OFFSET_LIMIT_MS);
                break;
            case Step::SCROLL_SPEED:
                scroll_speed = std::clamp(scroll_speed + direction * SCROLL_SPEED_STEP * (coarse ? 5 : 1), SCROLL_SPEED_MIN, SCROLL_SPEED_MAX);
                break;
        }
    }

    void SceneCalibration::record_tap(double event_time) {
        double position = clock.position_at(event_time);
        double nearest_beat = std::round(position / CLICK_BEAT_MS) * CLICK_BEAT_MS;

        tap_errors.push_back(position - nearest_beat);

        if (tap_errors.size() > MAX_TAP_SAMPLES) {
            tap_errors.pop_front();
        }

        if (tap_errors.size() >= MIN_TAPS_FOR_SUGGESTION) {
            input_offset = std::clamp((int32_t)std::lround(-get_average_tap_error()), -OFFSET_LIMIT_MS, OFFSET_LIMIT_MS);
        }
    }

    double SceneCalibration::get_average_tap_error() {
        if (tap_errors.empty()) {
            return 0;
        }

        double total = 0;

        for (double error : tap_errors) {
            total += error;
        }

        return total / tap_errors.size();
    }

    void SceneCalibration::finish() {
        SettingsManager::settings.field_audio_visual_offset = (int16_t)visual_offset;
        SettingsManager::settings.field_audio_input_offset = (int16_t)input_offset;
        SettingsManager::settings.field_scroll_speed = (uint16_t)scroll_speed;
        SettingsManager::save();

        game->play_sound(skin->get_random_menu_pluck());
        game->set_scene(return_scene);
    }

    void SceneCalibration::change_step(int32_t direction) {
        step = (Step)((int32_t)step + direction);
        game->play_sound(skin->hitsound_sfx);
    }

    void SceneCalibration::update(float) {
        olc::PixelGameEngine* window = game->window;
        double now = now_ms();

        if (click_audio_id != -1) {
            clock.sync(game->get_audio().GetCursorMilliseconds(click_audio_id), now);
        }

        bool coarse = window->GetKey(InputManager::keybind_mod1).bHeld;

        if (window->GetKey(InputManager::menu_left_key).bPressed) {
            adjust(-1, coarse);
        }

        if (window->GetKey(InputManager::menu_right_key).bPressed) {
            adjust(1, coarse);
        }

        if (step == Step::INPUT_OFFSET && window->GetKey(InputManager::calibration_reset_key).bPressed) {
            tap_errors.clear();
        }

        if (window->GetKey(InputManager::menu_start_key).bPressed) {
            finish();
            return;
        }

        if (window->GetKey(InputManager::menu_down_key).bPressed && step != Step::SCROLL_SPEED) {
            change_step(1);
        }

        if (window->GetKey(InputManager::menu_up_key).bPressed && step != Step::VISUAL_OFFSET) {
            change_step(-1);
        }

        for (const KeyEvent& event : game->get_frame_key_events()) {
            if (event.pressed && step == Step::INPUT_OFFSET && std::find(tap_keys.begin(), tap_keys.end(), event.key) != tap_keys.end()) {
                record_tap(event.time);
            }
        }

        if (field != nullptr) {
            field->scroll_speed = scroll_speed;
            field->set_position(clock.position_at(now) - visual_offset);
            field->set_keys(InputManager::get_lane_keys_held(window, lane_keys));
        }
    }

    void SceneCalibration::draw(olc::PixelGameEngine* window) {
        if (field != nullptr) {
            field->draw(window);
        }

        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        const char* step_names[] = {"Visual offset", "Input offset", "Scroll speed"};
        const std::vector<std::vector<const char*>> step_descriptions = {
            {"Shifts where notes are drawn relative to the music, in milliseconds.",
             "Corrects for display to audio latency so notes visually reach the judgement line on beat."},
            {"Shifts the moment when your key presses are judged, in milliseconds.",
             "Corrects for keyboard input to audio latency."},
            {"Scales how fast notes move toward the judgement line, as a percentage of the screen",
             "height per second. Higher speeds spread notes further apart."}
        };
        const std::vector<std::vector<const char*>> step_instructions = {
            {"Adjust until the notes reach the judgement line",
             "at the exact moment you hear each click."},
            {"Tap any lane key or Space along with the clicks.",
             "Offset is measured automatically, and can be fine tuned."},
            {"Higher scroll speeds are recommended for higher level play.",
             "Dense patterns become more spread out and easy to read."}
        };

        int step_index = (int)step;

        draw_text(skin->display_font_large, "Offset Calibration", {x, y}, olc::WHITE);
        y += 90;

        draw_text(skin->display_font_medium, std::format("Step {} of 3: {}", step_index + 1, step_names[step_index]), {x, y}, olc::WHITE);
        y += 50;

        for (const char* line : step_descriptions[step_index]) {
            draw_text(skin->display_font_small, line, {x, y}, olc::GREY);
            y += 26;
        }

        y += 20;

        for (const char* line : step_instructions[step_index]) {
            draw_text(skin->display_font_small, line, {x, y}, olc::WHITE);
            y += 26;
        }

        y += 34;

        std::string values[] = {
            std::format("{:+} ms", visual_offset),
            std::format("{:+} ms", input_offset),
            std::format("{}", scroll_speed)
        };

        for (int i = 0; i < 3; i++) {
            olc::Pixel color = i == step_index ? olc::WHITE : olc::DARK_GREY;
            draw_text(skin->display_font_medium, std::format("{}{}", i == step_index ? "> " : "  ", step_names[i]), {x, y}, color);
            draw_text(skin->display_font_medium, values[i], {x + 380, y}, color);
            y += 48;
        }

        y += 30;

        if (step == Step::INPUT_OFFSET) {
            std::string taps = tap_errors.empty()
                ? std::string("No taps yet")
                : std::format("{} taps, average error {:+.1f} ms{}", tap_errors.size(), get_average_tap_error(), tap_errors.size() < MIN_TAPS_FOR_SUGGESTION ? " (keep tapping)" : "");
            draw_text(skin->display_font_small, taps, {x, y}, olc::WHITE);
            y += 26;
            draw_text(skin->display_font_small, "R: clear taps", {x, y}, olc::GREY);
            y += 40;
        }

        if (!status_message.empty()) {
            draw_text(skin->display_font_small, status_message, {x, y}, olc::RED);
        }

        std::string footer = "↑/↓:  change step        ←/→:  adjust (Shift for bigger steps)        Enter:  save and exit        Esc:  exit";

        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, footer);
        int32_t footer_bottom = field != nullptr ? (int32_t)std::lround(field->get_judge_bottom()) : screen.y - screen.y / 12;

        draw_text(skin->display_font_small, footer, {x, footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y)}, olc::GREY);
    }
}
