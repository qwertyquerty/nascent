#include "KeyBindField.h"

#include "gse/render/Color.h"
#include "gse/ui/Ui.h"

#include <cmath>

namespace nascent {
    KeyBindField::KeyBindField(KeyBinding binding) : RowField(KEY_BINDINGS[(size_t)binding].label), binding(binding) {}

    std::string KeyBindField::display_value(bool) const {
        return InputManager::key_label(InputManager::get_key(binding));
    }

    olc::Pixel KeyBindField::value_color(UiTheme& theme, bool focused) const {
        if (!capturing) {
            return RowField::value_color(theme, focused);
        }

        olc::Pixel color = theme.accent(0.5);
        double low = Ui::layout("options.key_capture_min_alpha");
        double high = Ui::layout("options.key_capture_max_alpha");
        return fade(color, (low + (high - low) * (0.5 + 0.5 * std::cos(blink_timer * (float)Ui::layout("options.key_capture_pulse_speed")))) / 255.0);
    }

    void KeyBindField::on_blur() {
        capturing = false;
    }

    bool KeyBindField::update(Input& input, float elapsed_time) {
        RowField::update(input, elapsed_time);
        consumed = false;

        if (!capturing) {
            if (input.take(actions::CONFIRM)) {
                capturing = true;
                consumed = true;
                blink_timer = 0;
                return false;
            }

            return input.key_pressed(olc::Key::DEL) && reset_binding();
        }

        for (size_t index = 1; index < (size_t)olc::Key::ENUM_END; index++) {
            olc::Key key = (olc::Key)index;

            if (input.key_pressed(key) && InputManager::can_bind(key)) {
                swapped = InputManager::set_key(binding, key);
                capturing = false;
                consumed = true;
                return true;
            }
        }

        return false;
    }

    bool KeyBindField::click(olc::vf2d, Input&) {
        if (!capturing) {
            capturing = true;
            consumed = true;
            blink_timer = 0;
        }

        return false;
    }

    bool KeyBindField::consumes_navigation() const {
        return capturing || consumed;
    }

    bool KeyBindField::reset_binding() {
        if (InputManager::get_key(binding) == KEY_BINDINGS[(size_t)binding].default_key) {
            return false;
        }

        swapped = InputManager::reset_key(binding);
        return true;
    }

    bool KeyBindField::is_capturing() const {
        return capturing;
    }
}
