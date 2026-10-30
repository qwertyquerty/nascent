#include "EditorSettings.h"

#include <algorithm>
#include <fstream>

namespace nascent {
    namespace {
        template <typename T>
        T number(const nlohmann::json& data, const char* key, T fallback, T low, T high) {
            auto found = data.find(key);

            if (found == data.end() || !found->is_number()) {
                return fallback;
            }

            return std::clamp(found->get<T>(), low, high);
        }

        bool flag(const nlohmann::json& data, const char* key, bool fallback) {
            auto found = data.find(key);
            return found != data.end() && found->is_boolean() ? found->get<bool>() : fallback;
        }
    }

    EditorSettings load_editor_settings(const std::filesystem::path& file) {
        EditorSettings settings;
        std::ifstream in(file, std::ios::binary);

        if (!in) {
            return settings;
        }

        nlohmann::json data = nlohmann::json::parse(in, nullptr, false);

        if (!data.is_object()) {
            return settings;
        }

        settings.scroll_speed = number<uint32_t>(data, "scroll_speed", settings.scroll_speed, MIN_SCROLL_SPEED, MAX_SCROLL_SPEED);

        if (data.contains("lane_width") && data["lane_width"].is_number()) {
            settings.lane_width = std::clamp(data["lane_width"].get<int>(), MIN_LANE_WIDTH, MAX_LANE_WIDTH);
        }

        settings.judge_offset = number(data, "judge_offset", settings.judge_offset, 0.05f, 0.6f);
        settings.divisor = number<uint32_t>(data, "divisor", settings.divisor, 1, (uint32_t)MAX_SNAP_DIVISOR);
        settings.snap_enabled = flag(data, "snap_enabled", settings.snap_enabled);
        settings.hitsounds = flag(data, "hitsounds", settings.hitsounds);
        settings.metronome = flag(data, "metronome", settings.metronome);
        settings.snap_colors = flag(data, "snap_colors", settings.snap_colors);
        settings.music_volume = (uint8_t)number<int>(data, "music_volume", settings.music_volume, 0, 100);
        settings.hitsound_volume = (uint8_t)number<int>(data, "hitsound_volume", settings.hitsound_volume, 0, 100);
        settings.metronome_volume = (uint8_t)number<int>(data, "metronome_volume", settings.metronome_volume, 0, 100);
        settings.waveform_opacity = number(data, "waveform_opacity", settings.waveform_opacity, 0.0f, 1.0f);

        if (data.contains("note_opacity") && data["note_opacity"].is_number()) {
            settings.note_opacity = std::clamp(data["note_opacity"].get<float>(), 0.0f, 1.0f);
        }

        settings.playback_rate = number(data, "playback_rate", settings.playback_rate, 0.05f, 4.0f);
        settings.skin = data.value("skin", std::string());
        settings.gameplay_preview = flag(data, "gameplay_preview", settings.gameplay_preview);
        settings.timestamps_panel = flag(data, "timestamps_panel", settings.timestamps_panel);
        settings.ui_scale = number(data, "ui_scale", settings.ui_scale, MIN_UI_SCALE, MAX_UI_SCALE);
        settings.bindings.load(data.value("bindings", nlohmann::json::object()));
        return settings;
    }

    bool save_editor_settings(const std::filesystem::path& file, const EditorSettings& settings) {
        nlohmann::json data = {
            {"scroll_speed", settings.scroll_speed},
            {"judge_offset", settings.judge_offset},
            {"divisor", settings.divisor},
            {"snap_enabled", settings.snap_enabled},
            {"hitsounds", settings.hitsounds},
            {"metronome", settings.metronome},
            {"snap_colors", settings.snap_colors},
            {"music_volume", settings.music_volume},
            {"hitsound_volume", settings.hitsound_volume},
            {"metronome_volume", settings.metronome_volume},
            {"waveform_opacity", settings.waveform_opacity},
            {"playback_rate", settings.playback_rate},
            {"skin", settings.skin},
            {"gameplay_preview", settings.gameplay_preview},
            {"timestamps_panel", settings.timestamps_panel},
            {"ui_scale", settings.ui_scale},
            {"bindings", settings.bindings.to_json()},
        };

        if (settings.lane_width) {
            data["lane_width"] = *settings.lane_width;
        }

        if (settings.note_opacity) {
            data["note_opacity"] = *settings.note_opacity;
        }

        std::filesystem::path temporary = file;
        temporary += ".tmp";

        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);

            if (!out) {
                return false;
            }

            out << data.dump(1, '\t');

            if (!out) {
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::rename(temporary, file, ec);
        return !ec;
    }
}
