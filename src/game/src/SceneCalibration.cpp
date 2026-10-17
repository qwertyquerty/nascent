#include "SceneCalibration.h"
#include "RichPresence.h"
#include "Accuracy.h"
#include "Chart.h"
#include "EntityField.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "gse/input/KeyEvents.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "Utf8Path.h"
#include "gse/util/Math.h"

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

    SceneCalibration::SceneCalibration(Skin* skin) {
        this->skin = skin;
    }

    SceneCalibration::~SceneCalibration() {
    }

    void SceneCalibration::on_create() {
        RichPresence::in_menu("Calibrating offsets");
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

        field = std::make_unique<EntityField>(chart.get(), skin, olc::vd2d{0.0, 0.0}, olc::vd2d{1.0, 1.0});
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->upscroll = SettingsManager::settings.field_upscroll;
        field->mirror = SettingsManager::settings.field_mirror;
        field->note_gradient = SettingsManager::settings.field_note_gradient;
        on_resize();

        std::filesystem::path click_path = std::filesystem::temp_directory_path() / "nascent_calibration_click.wav";

        if (write_click_track(click_path)) {
            click_audio_id = game->audio().engine().LoadSound(click_path.string());
            game->audio().apply_effects_volume(click_audio_id);
            game->audio().engine().Play(click_audio_id, true);
        }
        else {
            status_message = std::format("Could not write the click track to {}", path_utf8(click_path));
        }

        clock.max_error = SettingsManager::settings.field_audio_sync_max_error;
        clock.reset(0, now_ms());
    }

    void SceneCalibration::on_exit() {
        if (click_audio_id != -1) {
            game->audio().engine().Stop(click_audio_id);
            game->audio().engine().UnloadSound(click_audio_id);
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

        game->audio().play_effect(skin->get_random_menu_pluck());
        game->scenes().pop();
    }

    void SceneCalibration::change_step(int32_t direction) {
        step = (Step)((int32_t)step + direction);
        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
    }

    bool SceneCalibration::update_mouse(Input& input, bool coarse) {
        if (int32_t wheel = input.take_wheel(); wheel != 0) {
            adjust(wheel > 0 ? 1 : -1, coarse);
        }

        std::optional<int32_t> clicked = spots.take_click(input);

        if (!clicked) {
            return false;
        }

        if (*clicked == SPOT_SAVE) {
            finish();
            return true;
        }

        if (*clicked == SPOT_CLEAR_TAPS) {
            tap_errors.clear();
            return false;
        }

        Step target = (Step)(*clicked % 10);

        if (target != step) {
            step = target;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (*clicked >= SPOT_PLUS) {
            adjust(1, coarse);
        }
        else if (*clicked >= SPOT_MINUS) {
            adjust(-1, coarse);
        }

        return false;
    }

    void SceneCalibration::update(float) {
        Input& input = game->input();
        double now = now_ms();

        if (click_audio_id != -1) {
            clock.sync(game->audio().engine().GetPositionMilliseconds(click_audio_id), now);
        }

        bool coarse = input.key_held(InputManager::keybind_mod1);

        if (update_mouse(input, coarse)) {
            return;
        }

        if (input.take(actions::LEFT)) {
            adjust(-1, coarse);
        }

        if (input.take(actions::RIGHT)) {
            adjust(1, coarse);
        }

        if (step == Step::INPUT_OFFSET && InputManager::reset_pressed(input)) {
            tap_errors.clear();
        }

        if (input.take(actions::CONFIRM) || input.take(actions::BACK)) {
            finish();
            return;
        }

        if (step != Step::SCROLL_SPEED && input.take(actions::DOWN)) {
            change_step(1);
        }

        if (step != Step::VISUAL_OFFSET && input.take(actions::UP)) {
            change_step(-1);
        }

        for (const KeyEvent& event : game->input().events()) {
            if (event.pressed && step == Step::INPUT_OFFSET && std::find(tap_keys.begin(), tap_keys.end(), event.key) != tap_keys.end()) {
                record_tap(event.time);
            }
        }

        if (field != nullptr) {
            field->scroll_speed = scroll_speed;
            field->set_position(clock.position_at(now) - visual_offset);
            field->set_keys(InputManager::get_lane_keys_held(input, lane_keys));
        }
    }

    void SceneCalibration::on_resize() {
        if (field == nullptr) {
            return;
        }

        olc::vi2d window_size = game->window->GetScreenSize();
        field->pos = {(double)window_size.x / 4 * 3 - (double)window_size.x / 24, 0.0};
        field->size = {(double)(window_size.x / 4), (double)window_size.y};
        field->judge_height = window_size.y * SettingsManager::settings.field_judge_line_percent / 100.0;
        field->set_lane_gap(SettingsManager::settings.field_lane_gap >= 0 ? std::optional<double>(Ui::pxf(SettingsManager::settings.field_lane_gap)) : std::nullopt);
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

        draw_text(skin->display_font_large, "Offset Calibration", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::px(90);

        draw_text(skin->display_font_medium, std::format("Step {} of 3: {}", step_index + 1, step_names[step_index]), {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::px(50);

        for (const char* line : step_descriptions[step_index]) {
            draw_text(skin->display_font_small, line, {x, y}, skin->color(SkinColor::TEXT_MUTED));
            y += Ui::px(26);
        }

        y += Ui::px(20);

        for (const char* line : step_instructions[step_index]) {
            draw_text(skin->display_font_small, line, {x, y}, skin->color(SkinColor::TEXT));
            y += Ui::px(26);
        }

        y += Ui::px(34);

        std::string values[] = {
            std::format("{:+} ms", visual_offset),
            std::format("{:+} ms", input_offset),
            std::format("{}", scroll_speed)
        };

        spots.clear();
        olc::Font& row_font = skin->display_font_medium;
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        olc::vf2d mouse = window->GetMousePos();

        for (int i = 0; i < 3; i++) {
            Rect row = {{(float)x, (float)(y + row_metrics.offset.y)}, {Ui::pxf(VALUE_COLUMN), (float)row_metrics.size.y}};
            spots.add(row.inflated(Ui::pxf(6)), SPOT_STEP + i);
            bool active = i == step_index || row.contains(mouse);
            olc::Pixel color = skin->color(active ? SkinColor::TEXT : SkinColor::TEXT_FAINT);
            draw_text(row_font, std::format("{}{}", i == step_index ? "> " : "  ", step_names[i]), {x, y}, color);
            draw_text(row_font, values[i], {x + Ui::px(VALUE_COLUMN), y}, color);
            float button_x = (float)(x + Ui::px(VALUE_COLUMN)) + text_bounds(row_font, "+000 ms").size.x + Ui::pxf(BUTTON_GAP);
            draw_hints(window, row_font, {{"-", SPOT_MINUS + i}, {"+", SPOT_PLUS + i}}, {button_x, (float)y}, Ui::pxf(BUTTON_GAP), skin->color(i == step_index ? SkinColor::TEXT_MUTED : SkinColor::TEXT_FAINT), skin->color(SkinColor::TEXT), spots);
            y += Ui::px(ROW_HEIGHT);
        }

        y += Ui::px(30);

        if (step == Step::INPUT_OFFSET) {
            std::string taps = tap_errors.empty()
                ? std::string("No taps yet")
                : std::format("{} taps, average error {:+.1f} ms{}", tap_errors.size(), get_average_tap_error(), tap_errors.size() < MIN_TAPS_FOR_SUGGESTION ? " (keep tapping)" : "");
            draw_text(skin->display_font_small, taps, {x, y}, skin->color(SkinColor::TEXT));
            y += Ui::px(26);
            draw_hints(window, skin->display_font_small, {{"Ctrl+R: clear taps", SPOT_CLEAR_TAPS}}, {(float)x, (float)y}, 0.0f, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);
            y += Ui::px(40);
        }

        if (!status_message.empty()) {
            draw_text(skin->display_font_small, status_message, {x, y}, skin->color(SkinColor::TEXT_ERROR));
        }

        std::vector<Hint> footer = {{"↑/↓:  change step"}, {"←/→ or wheel:  adjust (Shift for bigger steps)"}, {"Enter/Esc:  save and exit", SPOT_SAVE}};
        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, "Hgjy");
        int32_t footer_bottom = field != nullptr && !field->upscroll ? (int32_t)std::lround(field->get_judge_bottom()) : screen.y - screen.y / 12;

        draw_hints(window, skin->display_font_small, footer, {(float)x, (float)(footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y))}, Ui::pxf(FOOTER_GAP), skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);
    }
}
