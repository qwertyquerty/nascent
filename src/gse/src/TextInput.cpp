#include "TextInput.h"

namespace nascent {
    static int32_t offset(olc::Key key, olc::Key first) {
        return (int32_t)key - (int32_t)first;
    }

    static char us_layout_character(olc::Key key, bool shift) {
        if (key >= olc::Key::A && key <= olc::Key::Z) {
            return (char)((shift ? 'A' : 'a') + offset(key, olc::Key::A));
        }

        if (key >= olc::Key::K0 && key <= olc::Key::K9) {
            return shift ? ")!@#$%^&*("[offset(key, olc::Key::K0)] : (char)('0' + offset(key, olc::Key::K0));
        }

        if (key >= olc::Key::NP0 && key <= olc::Key::NP9) {
            return (char)('0' + offset(key, olc::Key::NP0));
        }

        switch (key) {
            case olc::Key::SPACE:
                return ' ';
            case olc::Key::NP_MUL:
                return '*';
            case olc::Key::NP_DIV:
                return '/';
            case olc::Key::NP_ADD:
                return '+';
            case olc::Key::NP_SUB:
                return '-';
            case olc::Key::NP_DECIMAL:
                return '.';
            case olc::Key::PERIOD:
                return shift ? '>' : '.';
            case olc::Key::COMMA:
                return shift ? '<' : ',';
            case olc::Key::EQUALS:
                return shift ? '+' : '=';
            case olc::Key::MINUS:
                return shift ? '_' : '-';
            case olc::Key::OEM_1:
                return shift ? ':' : ';';
            case olc::Key::OEM_2:
                return shift ? '?' : '/';
            case olc::Key::OEM_3:
                return shift ? '~' : '`';
            case olc::Key::OEM_4:
                return shift ? '{' : '[';
            case olc::Key::OEM_5:
                return shift ? '|' : '\\';
            case olc::Key::OEM_6:
                return shift ? '}' : ']';
            case olc::Key::OEM_7:
                return shift ? '"' : '\'';
            default:
                return '\0';
        }
    }

    std::string us_layout_text(olc::PixelGameEngine* window) {
        std::string text;

        if (window->GetKey(olc::Key::CTRL).bHeld) {
            return text;
        }

        bool shift = window->GetKey(olc::Key::SHIFT).bHeld;

        for (int32_t keycode : window->GetKeyPressCache()) {
            char character = us_layout_character(window->ConvertKeycode(keycode), shift);

            if (character != '\0') {
                text.push_back(character);
            }
        }

        return text;
    }
}
