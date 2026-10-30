#include "gse/input/KeyNames.h"

#include <array>

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

    std::optional<olc::Key> key_from_name(std::string_view name) {
        for (size_t i = 1; i < KEY_NAMES.size(); i++) {
            if (KEY_NAMES[i] == name) {
                return (olc::Key)i;
            }
        }

        return std::nullopt;
    }

    std::string_view key_name(olc::Key key) {
        return (size_t)key < KEY_NAMES.size() ? KEY_NAMES[(size_t)key] : KEY_NAMES[0];
    }
}
