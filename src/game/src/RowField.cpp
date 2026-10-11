#include "RowField.h"

#include "InputManager.h"
#include "Skin.h"
#include "TextCache.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <locale>
#include <sstream>
#include <utility>

namespace nascent {
    static constexpr int32_t CARET_INSET = 9;

    static int32_t text_width(olc::Font& font, const std::string& text) {
        return text.empty() ? 0 : text_bounds(font, text).size.x;
    }

    static std::string format_number(double value) {
        std::string text = std::format("{:.2f}", value);

        while (text.ends_with('0')) {
            text.pop_back();
        }

        if (text.ends_with('.')) {
            text.pop_back();
        }

        return text;
    }

    RowField::RowField(std::string label) : label(std::move(label)) {}

    bool RowField::is_placeholder() const {
        return false;
    }

    bool RowField::shows_caret() const {
        return false;
    }

    void RowField::on_focus(olc::PixelGameEngine*) {
        blink_timer = 0;
    }

    bool RowField::update(olc::PixelGameEngine*, const std::string&, float elapsed_time) {
        blink_timer += elapsed_time;
        return false;
    }

    int32_t RowField::draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position, bool focused) const {
        olc::Font& font = skin->display_font_small;
        olc::Pixel color = focused ? olc::WHITE : olc::GREY;

        window->FillRectDecal({(float)position.x, (float)position.y}, {(float)WIDTH, (float)HEIGHT}, olc::Pixel(255, 255, 255, focused ? 36 : 14));

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = position.y + (HEIGHT - metrics.size.y) / 2 - metrics.offset.y;
        draw_text(font, label, {position.x + PADDING, baseline}, color);

        std::string value = display_value(focused);
        int32_t value_x = position.x + WIDTH - PADDING - text_width(font, value);
        draw_text(font, value, {value_x, baseline}, is_placeholder() ? olc::DARK_GREY : color);

        if (focused && shows_caret() && std::fmod(blink_timer, 1.0f) < 0.5f) {
            int32_t caret = is_placeholder() ? position.x + WIDTH - PADDING : value_x + text_width(font, value) + 1;
            window->FillRectDecal({(float)caret, (float)(position.y + CARET_INSET)}, {2.0f, (float)(HEIGHT - CARET_INSET * 2)}, olc::WHITE);
        }

        return HEIGHT;
    }

    ChoiceField::ChoiceField(std::string label, std::vector<std::string> options, size_t selected) : RowField(std::move(label)), options(std::move(options)), selected(selected) {}

    std::string ChoiceField::display_value(bool focused) const {
        const std::string& value = options.empty() ? std::string() : options[std::min(selected, options.size() - 1)];
        return focused ? std::format("←  {}  →", value) : value;
    }

    bool ChoiceField::update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) {
        RowField::update(window, typed, elapsed_time);

        if (options.size() < 2) {
            return false;
        }

        if (window->GetKey(InputManager::menu_left_key).bPressed) {
            selected = (selected + options.size() - 1) % options.size();
            return true;
        }

        if (window->GetKey(InputManager::menu_right_key).bPressed) {
            selected = (selected + 1) % options.size();
            return true;
        }

        return false;
    }

    NumberField::NumberField(std::string label, std::string unit, double step, double maximum) : RowField(std::move(label)), unit(std::move(unit)), step(step), maximum(maximum) {}

    std::string NumberField::display_value(bool focused) const {
        if (text.empty()) {
            return focused ? std::string() : std::string("Any");
        }

        return unit.empty() ? text : std::format("{} {}", text, unit);
    }

    bool NumberField::is_placeholder() const {
        return text.empty();
    }

    bool NumberField::shows_caret() const {
        return true;
    }

    bool NumberField::update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) {
        RowField::update(window, typed, elapsed_time);
        std::string before = text;

        for (char c : typed) {
            if ((c >= '0' && c <= '9') || (c == '.' && text.find('.') == std::string::npos)) {
                text.push_back(c);
            }
        }

        for (int32_t keycode : window->GetKeyPressCache()) {
            olc::Key key = window->ConvertKeycode(keycode);

            if (key == olc::Key::BACK && !text.empty()) {
                text.pop_back();
            }
            else if (key == olc::Key::DEL) {
                text.clear();
            }
        }

        bool stepped = false;

        if (window->GetKey(InputManager::menu_left_key).bPressed || window->GetKey(InputManager::menu_right_key).bPressed) {
            std::string unstepped = text;
            double direction = window->GetKey(InputManager::menu_right_key).bPressed ? 1.0 : -1.0;
            double next = std::clamp(value().value_or(0.0) + step * direction, 0.0, maximum);
            text = next > 0.0 || direction > 0.0 ? format_number(next) : std::string();
            stepped = text != unstepped;
        }

        if (text != before) {
            blink_timer = 0;
        }

        return stepped;
    }

    std::optional<double> NumberField::value() const {
        if (text.empty() || text == ".") {
            return std::nullopt;
        }

        std::istringstream stream(text);
        stream.imbue(std::locale::classic());
        double parsed = 0;
        stream >> parsed;
        return stream.fail() ? std::nullopt : std::optional<double>(std::min(parsed, maximum));
    }
}
