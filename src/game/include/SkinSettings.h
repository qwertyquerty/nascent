#pragma once

#include "SettingsManager.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace nascent {
    class Skin;

    struct SkinSetting {
        std::optional<uint8_t>& (*value)(Settings&);
        const char* key;
    };

    struct SkinToggle {
        std::optional<bool>& (*value)(Settings&);
        const char* key;
    };

    namespace skin_toggles {
        inline constexpr SkinToggle SHOW_FPS = {[](Settings& settings) -> std::optional<bool>& { return settings.show_fps; }, "hud.show_fps"};
        inline constexpr SkinToggle SHOW_JUDGEMENT_LINE = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_judgement_line; }, "hud.show_judgement_line"};
        inline constexpr SkinToggle SHOW_JUDGEMENT = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_judgement; }, "hud.show_judgement"};
        inline constexpr SkinToggle SHOW_COMBO = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_combo; }, "hud.show_combo"};
        inline constexpr SkinToggle SHOW_ACCURACY = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_accuracy; }, "hud.show_accuracy"};
        inline constexpr SkinToggle SHOW_AVERAGE_ERROR = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_average_error; }, "hud.show_average_error"};
        inline constexpr SkinToggle SHOW_ERROR_DEVIATION = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_error_deviation; }, "hud.show_error_deviation"};
        inline constexpr SkinToggle SHOW_PERFORMANCE = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_performance; }, "hud.show_performance"};
        inline constexpr SkinToggle SHOW_JUDGEMENT_COUNTER = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_judgement_counter; }, "hud.show_judgement_counter"};
        inline constexpr SkinToggle SHOW_TIMING_BAR = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_timing_bar; }, "hud.show_timing_bar"};
        inline constexpr SkinToggle SHOW_TIMING_AVERAGE = {[](Settings& settings) -> std::optional<bool>& { return settings.field_show_timing_average; }, "hud.show_timing_average"};
        inline constexpr SkinToggle SNAP_COLORS = {[](Settings& settings) -> std::optional<bool>& { return settings.field_snap_colors; }, "playfield.snap_colors"};
        inline constexpr SkinToggle NOTE_GRADIENT = {[](Settings& settings) -> std::optional<bool>& { return settings.field_note_gradient; }, "playfield.note_gradient"};
        inline constexpr SkinToggle UPSCROLL = {[](Settings& settings) -> std::optional<bool>& { return settings.field_upscroll; }, "playfield.upscroll"};
    }

    namespace skin_settings {
        inline constexpr SkinSetting LANE_WIDTH = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_lane_width_percent; }, "hud.lane_width"};
        inline constexpr SkinSetting JUDGEMENT_LINE_HEIGHT = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_judge_line_percent; }, "hud.judgement_line_height"};
        inline constexpr SkinSetting JUDGEMENT_HEIGHT = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_judgement_height; }, "hud.judgement_height"};
        inline constexpr SkinSetting COMBO_HEIGHT = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_combo_height; }, "hud.combo_height"};
        inline constexpr SkinSetting TIMING_BAR_HEIGHT = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_timing_bar_height; }, "hud.timing_bar_height"};
        inline constexpr SkinSetting TIMING_BAR_LENGTH = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_timing_bar_length; }, "hud.timing_bar_length"};
        inline constexpr SkinSetting TIMING_BAR_WIDTH = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_timing_bar_width; }, "hud.timing_bar_width"};
        inline constexpr SkinSetting NOTE_OPACITY = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_note_opacity; }, "playfield.note_opacity"};
        inline constexpr SkinSetting RECEPTOR_OPACITY = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_receptor_opacity; }, "playfield.receptor_opacity"};
        inline constexpr SkinSetting LANE_GAP = {[](Settings& settings) -> std::optional<uint8_t>& { return settings.field_lane_gap; }, nullptr};
    }

    enum class HudElement : uint8_t {
        FPS,
        ACCURACY,
        AVERAGE_ERROR,
        PERFORMANCE,
        JUDGEMENT_COUNTER,
        PROGRESS,
        COUNT
    };

    struct HudSlot {
        const char* label;
        SkinSetting x;
        SkinSetting y;
    };

    inline constexpr std::array<HudSlot, (size_t)HudElement::COUNT> HUD_SLOTS = {{
        {"FPS counter", {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.fps_x; }, "hud.fps_x"}, {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.fps_y; }, "hud.fps_y"}},
        {"Accuracy", {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.accuracy_x; }, "hud.accuracy_x"}, {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.accuracy_y; }, "hud.accuracy_y"}},
        {"Average error", {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.error_x; }, "hud.error_x"}, {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.error_y; }, "hud.error_y"}},
        {"Perf prediction", {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.performance_x; }, "hud.performance_x"}, {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.performance_y; }, "hud.performance_y"}},
        {"Judgement count", {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.judgement_counter_x; }, "hud.judgement_counter_x"}, {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.judgement_counter_y; }, "hud.judgement_counter_y"}},
        {"Song progress", {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.progress_x; }, "hud.progress_x"}, {[](Settings& settings) -> std::optional<uint8_t>& { return settings.hud.progress_y; }, "hud.progress_y"}},
    }};

    double skin_setting(const Skin& skin, const SkinSetting& setting);
    std::optional<double> lane_gap_setting();
    double skin_lane_gap(const Skin& skin, uint8_t key_count);
    bool skin_toggle(const Skin& skin, const SkinToggle& toggle);
    bool skin_toggle(const SkinToggle& toggle);
    olc::vf2d hud_position(const Skin& skin, HudElement element, olc::vf2d size, olc::vi2d screen);
}
