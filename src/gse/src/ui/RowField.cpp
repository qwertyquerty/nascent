#include "gse/ui/RowField.h"

#include "gse/render/Color.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Layout.h"
#include "gse/ui/ProgressBar.h"
#include "gse/ui/Ui.h"

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

    olc::Pixel RowField::value_color(UiTheme& theme, bool focused) const {
        if (is_placeholder()) {
            return theme.color(UiColor::TEXT_FAINT);
        }

        return theme.color(focused ? UiColor::TEXT : UiColor::TEXT_MUTED);
    }

    bool RowField::is_placeholder() const {
        return false;
    }

    bool RowField::shows_caret() const {
        return false;
    }

    void RowField::on_focus() {
        blink_timer = 0;
    }

    bool RowField::update(Input&, float elapsed_time) {
        blink_timer += elapsed_time;
        return false;
    }

    int32_t RowField::draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const {
        olc::Font& font = theme.font(UiFont::SMALL);
        olc::Pixel color = theme.color(focused ? UiColor::TEXT : UiColor::TEXT_MUTED);

        int32_t row_width = width();
        int32_t row_height = height();
        int32_t padding = Ui::px(PADDING);
        int32_t caret_inset = Ui::px(CARET_INSET);
        bool hovered = Rect{{(float)position.x, (float)position.y}, {(float)row_width, (float)row_height}}.contains(window->GetMousePos());
        olc::Pixel background = theme.color(UiColor::INPUT_BACKGROUND);
        olc::Pixel focused_background = theme.color(UiColor::INPUT_BACKGROUND_FOCUSED);
        olc::Pixel hover_background = hovered ? mix(background, focused_background, HOVER_MIX) : background;
        window->FillRectDecal({(float)position.x, (float)position.y}, {(float)row_width, (float)row_height}, focused ? focused_background : hover_background);

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = position.y + (row_height - metrics.size.y) / 2 - metrics.offset.y;
        draw_text(font, label, {position.x + padding, baseline}, color);

        std::string value = display_value(focused);
        int32_t value_x = position.x + row_width - padding - text_width(font, value);
        value_left = value_x - position.x;
        value_right = value_left + text_width(font, value);
        draw_text(font, value, {value_x, baseline}, value_color(theme, focused));

        if (focused && shows_caret() && std::fmod(blink_timer, 1.0f) < 0.5f) {
            int32_t caret = is_placeholder() ? position.x + row_width - padding : value_x + text_width(font, value) + 1;
            window->FillRectDecal({(float)caret, (float)(position.y + caret_inset)}, {std::max(1.0f, Ui::pxf(2)), (float)(row_height - caret_inset * 2)}, theme.color(UiColor::TEXT));
        }

        return row_height;
    }

    int32_t RowField::width() {
        return Ui::px(WIDTH);
    }

    int32_t RowField::height() {
        return Ui::px(HEIGHT);
    }

    ChoiceField::ChoiceField(std::string label, std::vector<std::string> options, size_t selected) : RowField(std::move(label)), options(std::move(options)), selected(selected) {}

    const std::string& ChoiceField::current() const {
        static const std::string empty;
        return options.empty() ? empty : options[std::min(selected, options.size() - 1)];
    }

    std::string ChoiceField::display_value(bool) const {
        return std::string();
    }

    int32_t ChoiceField::draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const {
        int32_t row_height = RowField::draw(window, theme, position, focused);
        olc::Font& font = theme.font(UiFont::SMALL);
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = position.y + (row_height - metrics.size.y) / 2 - metrics.offset.y;
        float gap = Ui::pxf(ARROW_GAP);
        float left_width = (float)text_width(font, "←");
        float right_width = (float)text_width(font, "→");
        float widest = 0;

        for (const std::string& option : options) {
            widest = std::max(widest, (float)text_width(font, option));
        }

        float right_x = (float)(width() - Ui::px(PADDING)) - right_width;
        float slot_right = right_x - gap;
        float slot_left = slot_right - widest;
        float left_x = slot_left - gap - left_width;
        previous_left = left_x;
        previous_right = left_x + left_width;
        value_left = (int32_t)slot_left;
        value_right = (int32_t)slot_right;

        const std::string& value = current();
        float value_x = slot_left + (widest - (float)text_width(font, value)) / 2.0f;
        draw_text(font, value, olc::vf2d{(float)position.x + value_x, (float)baseline}, value_color(theme, focused), 1.0f);

        olc::vf2d mouse = olc::vf2d(window->GetMousePos()) - olc::vf2d{(float)position.x, (float)position.y};
        bool hovered = mouse.x >= 0 && mouse.y >= 0 && mouse.x <= width() && mouse.y <= row_height;

        if (options.size() < 2) {
            return row_height;
        }

        float slack = Ui::pxf(ARROW_SLACK);
        bool over_previous = hovered && mouse.x >= left_x - slack && mouse.x <= previous_right + slack;
        bool over_next = hovered && !over_previous && mouse.x >= slot_left;
        olc::Pixel idle = theme.color(focused || hovered ? UiColor::TEXT_MUTED : UiColor::TEXT_FAINT);
        olc::Pixel active = theme.color(UiColor::TEXT);
        draw_text(font, "←", olc::vf2d{(float)position.x + left_x, (float)baseline}, over_previous ? active : idle, 1.0f);
        draw_text(font, "→", olc::vf2d{(float)position.x + right_x, (float)baseline}, over_next ? active : idle, 1.0f);
        return row_height;
    }

    bool ChoiceField::update(Input& input, float elapsed_time) {
        RowField::update(input, elapsed_time);

        if (options.size() < 2) {
            return false;
        }

        if (input.take(actions::LEFT)) {
            selected = (selected + options.size() - 1) % options.size();
            return true;
        }

        if (input.take(actions::RIGHT)) {
            selected = (selected + 1) % options.size();
            return true;
        }

        return false;
    }

    bool ChoiceField::click(olc::vf2d point, Input&) {
        if (options.size() < 2) {
            return false;
        }

        float slack = Ui::pxf(ARROW_SLACK);
        bool previous = point.x >= previous_left - slack && point.x <= previous_right + slack;
        selected = previous ? (selected + options.size() - 1) % options.size() : (selected + 1) % options.size();
        return true;
    }

    SliderField::SliderField(std::string label, int32_t value, int32_t minimum, int32_t maximum, int32_t step, std::string unit) : RowField(std::move(label)), value(std::clamp(value, minimum, maximum)), minimum(minimum), maximum(maximum), step(step), unit(std::move(unit)) {}

    std::string SliderField::display_value(bool) const {
        return std::format("{}{}", value, unit);
    }

    bool SliderField::update(Input& input, float elapsed_time) {
        RowField::update(input, elapsed_time);
        int32_t before = value;
        bool left = left_repeat.update(input.pressed(actions::LEFT), input.held(actions::LEFT), elapsed_time);
        bool right = right_repeat.update(input.pressed(actions::RIGHT), input.held(actions::RIGHT), elapsed_time);

        if (left) {
            input.consume(actions::LEFT);
            value = std::max(minimum, value - step);
        }

        if (right) {
            input.consume(actions::RIGHT);
            value = std::min(maximum, value + step);
        }

        return value != before;
    }

    bool SliderField::set_from_point(float x) {
        float bar_left = Ui::pxf(BAR_LEFT);
        float bar_width = Ui::pxf(WIDTH - BAR_LEFT - BAR_RIGHT_GAP);
        float fraction = std::clamp((x - bar_left) / std::max(1.0f, bar_width), 0.0f, 1.0f);
        int32_t steps = step > 0 ? (int32_t)std::lround(fraction * (maximum - minimum) / step) : 0;
        int32_t next = std::clamp(minimum + steps * step, minimum, maximum);

        if (next == value) {
            return false;
        }

        value = next;
        return true;
    }

    bool SliderField::click(olc::vf2d point, Input&) {
        dragging_bar = point.x >= Ui::pxf(BAR_LEFT) - Ui::pxf(PADDING) && point.x <= Ui::pxf(WIDTH - BAR_RIGHT_GAP) + Ui::pxf(PADDING);
        return dragging_bar && set_from_point(point.x);
    }

    bool SliderField::drag(olc::vf2d point) {
        return dragging_bar && set_from_point(point.x);
    }

    int32_t SliderField::draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const {
        int32_t row_height = RowField::draw(window, theme, position, focused);
        float bar_width = Ui::pxf(WIDTH - BAR_LEFT - BAR_RIGHT_GAP);
        float bar_height = std::max(1.0f, Ui::pxf(BAR_HEIGHT));
        float fill = maximum > minimum ? (float)(value - minimum) / (float)(maximum - minimum) : 0.0f;
        olc::Pixel color = theme.accent(fill);
        color.a = focused ? 255 : 140;

        ProgressBar({bar_width, bar_height}, theme.color(UiColor::PROGRESS_TRACK)).draw(window, {(float)position.x + Ui::pxf(BAR_LEFT), (float)position.y + (row_height - bar_height) / 2}, fill, color);
        return row_height;
    }

    ButtonField::ButtonField(std::string label) : RowField(std::move(label)) {}

    std::string ButtonField::display_value(bool) const {
        return std::string();
    }

    bool ButtonField::update(Input& input, float elapsed_time) {
        RowField::update(input, elapsed_time);
        consumed = input.take(actions::CONFIRM);
        pressed = pressed || consumed;
        return consumed;
    }

    bool ButtonField::click(olc::vf2d, Input&) {
        pressed = true;
        return true;
    }

    bool ButtonField::consumes_navigation() const {
        return consumed;
    }

    bool ButtonField::take_pressed() {
        return std::exchange(pressed, false);
    }

    SectionField::SectionField(std::string title) : title(std::move(title)) {}

    int32_t SectionField::draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool) const {
        olc::Font& font = theme.font(UiFont::SMALL);
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t text_top = position.y + Ui::px(TOP_GAP);
        draw_text(font, title, {position.x, text_top - metrics.offset.y}, theme.color(UiColor::TEXT));

        int32_t line_top = text_top + metrics.size.y + Ui::px(LINE_GAP);
        int32_t line_height = std::max(1, Ui::px(LINE_HEIGHT));
        window->FillRectDecal({(float)position.x, (float)line_top}, {(float)RowField::width(), (float)line_height}, theme.color(UiColor::SEPARATOR));
        return line_top + line_height - position.y;
    }

    bool SectionField::focusable() const {
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

    bool NumberField::update(Input& input, float elapsed_time) {
        RowField::update(input, elapsed_time);
        const std::string& typed = input.text();
        std::string before = text;

        for (char c : typed) {
            if ((c >= '0' && c <= '9') || (c == '.' && text.find('.') == std::string::npos)) {
                text.push_back(c);
            }
        }

        for (olc::Key key : input.pressed_keys()) {
            if (key == olc::Key::BACK && !text.empty()) {
                text.pop_back();
            }
            else if (key == olc::Key::DEL) {
                text.clear();
            }
        }

        bool stepped = false;

        bool step_left = input.take(actions::LEFT);
        bool step_right = !step_left && input.take(actions::RIGHT);

        if (step_left || step_right) {
            std::string unstepped = text;
            double direction = step_right ? 1.0 : -1.0;
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
