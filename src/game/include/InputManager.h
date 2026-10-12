#pragma once

#include "olcPixelGameEngine.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nascent {
    class InputManager {
        public:
        static const olc::Key pause_key = olc::Key::TAB;

        static const olc::Key options_key = olc::Key::F1;
        static const olc::Key random_chart_key = olc::Key::F2;
        static const olc::Key calibration_key = olc::Key::F3;
        static const olc::Key account_key = olc::Key::F4;
        static const olc::Key browse_key = olc::Key::F5;
        static const olc::Key input_key = olc::Key::F6;
        static const olc::Key field_key = olc::Key::TAB;
        static const olc::Key switch_key = olc::Key::TAB;
        static const olc::Key leaderboard_key = olc::Key::TAB;
        static const olc::Key hit_log_key = olc::Key::F10;
        static const olc::Key diagnostics_copy_key = olc::Key::F11;
        static const olc::Key restart_key = olc::Key::OEM_3;
        static const olc::Key switch_modifier = olc::Key::CTRL;
        static const olc::Key calibration_reset_key = olc::Key::R;

        static const olc::Key menu_up_key = olc::Key::UP;
        static const olc::Key menu_down_key = olc::Key::DOWN;
        static const olc::Key menu_left_key = olc::Key::LEFT;
        static const olc::Key menu_right_key = olc::Key::RIGHT;

        static const olc::Key menu_start_key = olc::Key::ENTER;

        static const olc::Key exit_key = olc::Key::PAUSE;

        static const olc::Key back_key = olc::Key::ESCAPE;

        static const olc::Key keybind_mod1 = olc::Key::SHIFT;

        static bool switch_pressed(olc::PixelGameEngine* window);
        static std::optional<olc::Key> key_from_name(std::string_view name);
        static std::string_view key_name(olc::Key key);
        static std::string key_label(olc::Key key);
        static std::vector<olc::Key> default_lane_keys(uint8_t key_count);
        static std::vector<olc::Key> get_lane_keys(uint8_t key_count);
        static bool is_custom_lane_keys(uint8_t key_count);
        static bool set_lane_keys(uint8_t key_count, const std::vector<olc::Key>& keys);
        static bool reset_lane_keys(uint8_t key_count);
        static std::optional<uint8_t> get_lane(const std::vector<olc::Key>& lane_keys, olc::Key key);
        static uint32_t get_lane_keys_held(olc::PixelGameEngine* window, const std::vector<olc::Key>& lane_keys);
    };
}
