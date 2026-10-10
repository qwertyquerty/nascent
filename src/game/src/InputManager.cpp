#include "InputManager.h"
#include "SettingsManager.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <string>

namespace nascent {
    static constexpr std::array<std::string_view, (size_t)olc::Key::ENUM_END> KEY_NAMES = {
        "NONE",
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "K0", "K1", "K2", "K3", "K4", "K5", "K6", "K7", "K8", "K9",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
        "UP", "DOWN", "LEFT", "RIGHT",
        "SPACE", "TAB", "SHIFT", "CTRL", "INS", "DEL", "HOME", "END", "PGUP", "PGDN",
        "BACK", "ESCAPE", "RETURN", "ENTER", "PAUSE", "SCROLL",
        "NP0", "NP1", "NP2", "NP3", "NP4", "NP5", "NP6", "NP7", "NP8", "NP9",
        "NP_MUL", "NP_DIV", "NP_ADD", "NP_SUB", "NP_DECIMAL", "PERIOD",
        "EQUALS", "COMMA", "MINUS",
        "OEM_1", "OEM_2", "OEM_3", "OEM_4", "OEM_5", "OEM_6", "OEM_7", "OEM_8",
        "CAPS_LOCK"
    };

    static_assert(KEY_NAMES.back() == "CAPS_LOCK" && KEY_NAMES.size() == (size_t)olc::Key::CAPS_LOCK + 1);

    std::optional<olc::Key> InputManager::key_from_name(std::string_view name) {
        for (size_t i = 1; i < KEY_NAMES.size(); i++) {
            if (KEY_NAMES[i] == name) {
                return (olc::Key)i;
            }
        }

        return std::nullopt;
    }

    std::vector<olc::Key> InputManager::default_lane_keys(uint8_t key_count) {
        static constexpr std::array<olc::Key, 5> left = {olc::Key::Z, olc::Key::X, olc::Key::C, olc::Key::V, olc::Key::B};
        static constexpr std::array<olc::Key, 5> right = {olc::Key::OEM_2, olc::Key::PERIOD, olc::Key::COMMA, olc::Key::M, olc::Key::N};

        size_t half = std::min<size_t>(key_count / 2, left.size());
        std::vector<olc::Key> keys(left.begin(), left.begin() + half);

        if (key_count % 2 == 1) {
            keys.push_back(olc::Key::SPACE);
        }

        for (size_t i = half; i > 0; i--) {
            keys.push_back(right[i - 1]);
        }

        return keys;
    }

    std::vector<olc::Key> InputManager::get_lane_keys(uint8_t key_count) {
        auto keymap = SettingsManager::settings.lane_keymaps.find(std::to_string(key_count));

        if (keymap == SettingsManager::settings.lane_keymaps.end()) {
            return default_lane_keys(key_count);
        }

        std::vector<olc::Key> keys;

        for (const std::string& name : keymap->second) {
            std::optional<olc::Key> key = key_from_name(name);

            if (!key) {
                std::cerr << "Unknown key \"" << name << "\" in " << (int)key_count << "K keymap, using default keymap" << std::endl;
                return default_lane_keys(key_count);
            }

            keys.push_back(*key);
        }

        if (keys.size() != key_count) {
            std::cerr << (int)key_count << "K keymap has " << keys.size() << " keys, using default keymap" << std::endl;
            return default_lane_keys(key_count);
        }

        return keys;
    }

    std::optional<uint8_t> InputManager::get_lane(const std::vector<olc::Key>& lane_keys, olc::Key key) {
        for (size_t lane = 0; lane < lane_keys.size(); lane++) {
            if (lane_keys[lane] == key) {
                return (uint8_t)lane;
            }
        }

        return std::nullopt;
    }

    uint32_t InputManager::get_lane_keys_held(olc::PixelGameEngine* window, const std::vector<olc::Key>& lane_keys) {
        uint32_t keys = 0;

        for (size_t lane = 0; lane < lane_keys.size(); lane++) {
            if (window->GetKey(lane_keys[lane]).bHeld) {
                keys |= (1u << lane);
            }
        }

        return keys;
    }
}
