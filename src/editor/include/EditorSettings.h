#pragma once

#include "EditorBindings.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace nascent {
    inline constexpr uint32_t MIN_SCROLL_SPEED = 25;
    inline constexpr uint32_t MAX_SCROLL_SPEED = 500;
    inline constexpr int MIN_LANE_WIDTH = 3;
    inline constexpr int MAX_LANE_WIDTH = 30;
    inline constexpr int MAX_SNAP_DIVISOR = 96;
    inline constexpr float MIN_UI_SCALE = 0.5f;
    inline constexpr float MAX_UI_SCALE = 3.0f;

    struct EditorSettings {
        uint32_t scroll_speed = 160;
        std::optional<int> lane_width;
        float judge_offset = 0.2f;
        uint32_t divisor = 4;
        bool snap_enabled = true;
        bool hitsounds = true;
        bool metronome = false;
        bool snap_colors = true;
        uint8_t music_volume = 80;
        uint8_t hitsound_volume = 60;
        uint8_t metronome_volume = 70;
        float waveform_opacity = 0.2f;
        std::optional<float> note_opacity;
        float playback_rate = 1.0f;
        std::string skin;
        bool gameplay_preview = false;
        bool timestamps_panel = false;
        float ui_scale = 1.0f;
        EditorBindings bindings;

        bool operator==(const EditorSettings& other) const = default;
    };

    EditorSettings load_editor_settings(const std::filesystem::path& file);
    bool save_editor_settings(const std::filesystem::path& file, const EditorSettings& settings);
}
