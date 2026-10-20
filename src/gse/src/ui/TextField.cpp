#include "gse/ui/TextField.h"

#include "gse/render/Color.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Layout.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nascent {
    static bool is_continuation(char c) {
        return ((unsigned char)c & 0xc0) == 0x80;
    }

    static size_t next_character(const std::string& text, size_t index) {
        if (index >= text.size()) {
            return text.size();
        }

        index++;

        while (index < text.size() && is_continuation(text[index])) {
            index++;
        }

        return index;
    }

    static size_t previous_character(const std::string& text, size_t index) {
        if (index == 0) {
            return 0;
        }

        index--;

        while (index > 0 && is_continuation(text[index])) {
            index--;
        }

        return index;
    }

    static size_t character_count(const std::string& text, size_t end) {
        size_t count = 0;

        for (size_t i = 0; i < end; i = next_character(text, i)) {
            count++;
        }

        return count;
    }

    static int32_t text_width(olc::Font& font, const std::string& text) {
        return text.empty() ? 0 : text_bounds(font, text).size.x;
    }

    TextField::TextField(std::string label, std::string value, bool secret) : label(std::move(label)), value(std::move(value)), secret(secret) {
        cursor = this->value.size();
    }

    void TextField::on_focus() {
        blink_timer = 0;
        cursor = value.size();
    }

    bool TextField::update(Input& input, float elapsed_time) {
        const std::string& typed = input.text();
        size_t before = cursor;
        std::string old_value = value;
        blink_timer += elapsed_time;
        cursor = std::min(cursor, value.size());

        for (olc::Key key : input.pressed_keys()) {
            switch (key) {
                case olc::Key::BACK:
                    if (cursor > 0) {
                        size_t start = previous_character(value, cursor);
                        value.erase(start, cursor - start);
                        cursor = start;
                    }
                    break;
                case olc::Key::DEL:
                    value.erase(cursor, next_character(value, cursor) - cursor);
                    break;
                case olc::Key::LEFT:
                    cursor = previous_character(value, cursor);
                    input.consume(actions::LEFT);
                    break;
                case olc::Key::RIGHT:
                    cursor = next_character(value, cursor);
                    input.consume(actions::RIGHT);
                    break;
                case olc::Key::HOME:
                    cursor = 0;
                    break;
                case olc::Key::END:
                    cursor = value.size();
                    break;
                default:
                    break;
            }
        }

        value.insert(cursor, typed);
        cursor += typed.size();

        if (cursor != before || value != old_value) {
            blink_timer = 0;
        }

        return false;
    }

    int32_t TextField::draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const {
        olc::Font& label_font = theme.font(UiFont::SMALL);
        olc::Font& text_font = theme.font(UiFont::MEDIUM);
        olc::Pixel color = theme.color(focused ? UiColor::TEXT : UiColor::TEXT_MUTED);

        olc::FontRect label_metrics = text_bounds(label_font, "Hgjy");
        draw_text(label_font, label, {position.x, position.y - label_metrics.offset.y}, color);

        int32_t box_width = width();
        int32_t box_height = Ui::layout_px("form.text_field_height");
        int32_t padding = Ui::layout_px("form.text_field_padding");
        int32_t caret_inset = Ui::layout_px("form.text_field_caret_inset");
        int32_t box_top = position.y + label_metrics.size.y + Ui::layout_px("form.text_field_label_gap");
        bool hovered = Rect{{(float)position.x, (float)box_top}, {(float)box_width, (float)box_height}}.contains(window->GetMousePos());
        olc::Pixel background = theme.color(UiColor::INPUT_BACKGROUND);
        olc::Pixel focused_background = theme.color(UiColor::INPUT_BACKGROUND_FOCUSED);
        olc::Pixel hover_background = hovered ? mix(background, focused_background, Ui::layout("form.hover_mix")) : background;
        window->FillRectDecal({(float)position.x, (float)box_top}, {(float)box_width, (float)box_height}, focused ? focused_background : hover_background);

        size_t end = focused ? std::min(cursor, value.size()) : value.size();
        std::string shown = secret ? std::string(character_count(value, value.size()), '*') : value;
        size_t caret_index = secret ? character_count(value, end) : end;
        size_t first = 0;
        int32_t room = box_width - padding * 2;

        while (first < caret_index && text_width(text_font, shown.substr(first, caret_index - first)) > room) {
            first = next_character(shown, first);
        }

        std::string visible = shown.substr(first);

        while (!visible.empty() && text_width(text_font, visible) > room) {
            visible.erase(previous_character(visible, visible.size()));
        }

        olc::FontRect text_metrics = text_bounds(text_font, "Hgjy");
        int32_t baseline = box_top + (box_height - text_metrics.size.y) / 2 - text_metrics.offset.y;
        int32_t text_left = position.x + padding;
        draw_text(text_font, visible, {text_left, baseline}, color);

        if (focused && std::fmod(blink_timer, (float)Ui::layout("form.caret_blink_period")) < (float)Ui::layout("form.caret_visible_time")) {
            int32_t caret = text_left + text_width(text_font, shown.substr(first, caret_index - first)) + 1;
            window->FillRectDecal({(float)caret, (float)(box_top + caret_inset)}, {std::max(1.0f, Ui::layout_pxf("form.caret_width")), (float)(box_height - caret_inset * 2)}, theme.color(UiColor::TEXT));
        }

        return box_top + box_height - position.y;
    }

    int32_t TextField::width() {
        return Ui::layout_px("form.text_field_width");
    }

    bool TextField::is_filled() const {
        return !value.empty();
    }
}
