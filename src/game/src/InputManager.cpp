#include "InputManager.h"
#include "SettingsManager.h"

#include <algorithm>
#include <array>
#include <format>
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

    bool InputManager::switch_pressed(olc::PixelGameEngine* window) {
        return window->GetKey(switch_modifier).bHeld && window->GetKey(switch_key).bPressed;
    }

    bool InputManager::reset_pressed(olc::PixelGameEngine* window) {
        return window->GetKey(reset_modifier).bHeld && window->GetKey(reset_shortcut_key).bPressed;
    }

    bool InputManager::update_pressed(olc::PixelGameEngine* window) {
        return window->GetKey(update_modifier).bHeld && window->GetKey(update_key).bPressed;
    }

    std::optional<olc::Key> InputManager::key_from_name(std::string_view name) {
        for (size_t i = 1; i < KEY_NAMES.size(); i++) {
            if (KEY_NAMES[i] == name) {
                return (olc::Key)i;
            }
        }

        return std::nullopt;
    }

    std::string_view InputManager::key_name(olc::Key key) {
        return (size_t)key < KEY_NAMES.size() ? KEY_NAMES[(size_t)key] : KEY_NAMES[0];
    }

    std::string InputManager::key_label(olc::Key key) {
        if (key >= olc::Key::K0 && key <= olc::Key::K9) {
            return std::string(1, (char)('0' + ((int)key - (int)olc::Key::K0)));
        }

        if (key >= olc::Key::NP0 && key <= olc::Key::NP9) {
            return std::format("Num {}", (int)key - (int)olc::Key::NP0);
        }

        switch (key) {
            case olc::Key::UP:
                return "↑";
            case olc::Key::DOWN:
                return "↓";
            case olc::Key::LEFT:
                return "←";
            case olc::Key::RIGHT:
                return "→";
            case olc::Key::SPACE:
                return "Space";
            case olc::Key::TAB:
                return "Tab";
            case olc::Key::SHIFT:
                return "Shift";
            case olc::Key::CTRL:
                return "Ctrl";
            case olc::Key::INS:
                return "Insert";
            case olc::Key::DEL:
                return "Delete";
            case olc::Key::HOME:
                return "Home";
            case olc::Key::END:
                return "End";
            case olc::Key::PGUP:
                return "Page Up";
            case olc::Key::PGDN:
                return "Page Down";
            case olc::Key::BACK:
                return "Backspace";
            case olc::Key::ESCAPE:
                return "Esc";
            case olc::Key::RETURN:
                return "Return";
            case olc::Key::ENTER:
                return "Enter";
            case olc::Key::PAUSE:
                return "Pause";
            case olc::Key::SCROLL:
                return "Scroll Lock";
            case olc::Key::NP_MUL:
                return "Num *";
            case olc::Key::NP_DIV:
                return "Num /";
            case olc::Key::NP_ADD:
                return "Num +";
            case olc::Key::NP_SUB:
                return "Num -";
            case olc::Key::NP_DECIMAL:
                return "Num .";
            case olc::Key::PERIOD:
                return ".";
            case olc::Key::EQUALS:
                return "=";
            case olc::Key::COMMA:
                return ",";
            case olc::Key::MINUS:
                return "-";
            case olc::Key::OEM_1:
                return ";";
            case olc::Key::OEM_2:
                return "/";
            case olc::Key::OEM_3:
                return "`";
            case olc::Key::OEM_4:
                return "[";
            case olc::Key::OEM_5:
                return "\\";
            case olc::Key::OEM_6:
                return "]";
            case olc::Key::OEM_7:
                return "'";
            case olc::Key::OEM_8:
                return "OEM 8";
            case olc::Key::CAPS_LOCK:
                return "Caps Lock";
            default:
                return std::string(key_name(key));
        }
    }

    bool InputManager::is_custom_lane_keys(uint8_t key_count) {
        return get_lane_keys(key_count) != default_lane_keys(key_count);
    }

    bool InputManager::set_lane_keys(uint8_t key_count, const std::vector<olc::Key>& keys) {
        std::vector<std::string> names;

        for (olc::Key key : keys) {
            names.emplace_back(key_name(key));
        }

        SettingsManager::settings.lane_keymaps[std::to_string(key_count)] = names;
        return SettingsManager::save();
    }

    bool InputManager::reset_lane_keys(uint8_t key_count) {
        SettingsManager::settings.lane_keymaps.erase(std::to_string(key_count));
        return SettingsManager::save();
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

    static const KeyBindingInfo& binding_info(KeyBinding binding) {
        return KEY_BINDINGS[(size_t)binding];
    }

    olc::Key InputManager::get_key(KeyBinding binding) {
        const KeyBindingInfo& info = binding_info(binding);
        auto bound = SettingsManager::settings.key_bindings.find(info.id);

        if (bound == SettingsManager::settings.key_bindings.end()) {
            return info.default_key;
        }

        return key_from_name(bound->second).value_or(info.default_key);
    }

    bool InputManager::can_bind(olc::Key key) {
        return key != olc::Key::NONE && key != back_key && key != exit_key;
    }

    std::optional<KeyBinding> InputManager::set_key(KeyBinding binding, olc::Key key) {
        if (!can_bind(key)) {
            return std::nullopt;
        }

        olc::Key previous = get_key(binding);
        std::optional<KeyBinding> swapped;

        auto store = [](KeyBinding target, olc::Key value) {
            const KeyBindingInfo& info = binding_info(target);
            std::map<std::string, std::string>& bindings = SettingsManager::settings.key_bindings;

            if (value == info.default_key) {
                bindings.erase(info.id);
            }
            else {
                bindings[info.id] = std::string(key_name(value));
            }
        };

        for (size_t index = 0; index < KEY_BINDINGS.size(); index++) {
            KeyBinding other = (KeyBinding)index;

            if (other != binding && get_key(other) == key) {
                store(other, previous);
                swapped = other;
            }
        }

        store(binding, key);
        return swapped;
    }

    std::optional<KeyBinding> InputManager::reset_key(KeyBinding binding) {
        return set_key(binding, binding_info(binding).default_key);
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
