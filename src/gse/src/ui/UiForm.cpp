#include "gse/ui/UiForm.h"

#include "gse/ui/Layout.h"
#include "gse/ui/RowField.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <cstdlib>

namespace nascent {
    void UiForm::clear() {
        if (!fields.empty()) {
            fields[focus]->on_blur();
        }

        fields.clear();
        focus = 0;
        enabled = true;
    }

    std::optional<size_t> UiForm::step_focus(size_t from, bool forward, bool wrap) const {
        for (size_t step = 1; step <= fields.size(); step++) {
            if (!wrap && (forward ? from + step >= fields.size() : step > from)) {
                return std::nullopt;
            }

            size_t index = forward ? (from + step) % fields.size() : (from + fields.size() - step) % fields.size();

            if (index == from) {
                return std::nullopt;
            }

            if (fields[index]->focusable()) {
                return index;
            }
        }

        return std::nullopt;
    }

    void UiForm::focus_field(size_t index) {
        if (fields.empty()) {
            return;
        }

        index = std::min(index, fields.size() - 1);

        if (!fields[index]->focusable()) {
            std::optional<size_t> nearest = step_focus(index, true, false);

            if (!nearest) {
                nearest = step_focus(index, false, false);
            }

            if (!nearest) {
                return;
            }

            index = *nearest;
        }

        fields[focus]->on_blur();
        focus = index;

        if (enabled) {
            fields[focus]->on_focus();
        }
    }

    void UiForm::focus_nearest(int32_t y) {
        std::optional<size_t> nearest;
        int32_t best = 0;

        for (size_t index = 0; index < field_tops.size() && index < fields.size(); index++) {
            if (!fields[index]->focusable()) {
                continue;
            }

            int32_t distance = std::abs((field_tops[index] + field_bottoms[index]) / 2 - y);

            if (!nearest || distance < best) {
                nearest = index;
                best = distance;
            }
        }

        if (nearest && *nearest != focus) {
            focus_field(*nearest);
        }
    }

    void UiForm::set_enabled(bool enabled) {
        this->enabled = enabled;

        if (fields.empty()) {
            return;
        }

        if (enabled) {
            fields[focus]->on_focus();
        }
        else {
            fields[focus]->on_blur();
        }
    }

    bool UiForm::is_enabled() const {
        return enabled;
    }

    bool UiForm::is_empty() const {
        return fields.empty();
    }

    const UiField* UiForm::first_unfilled() const {
        for (const auto& field : fields) {
            if (!field->is_filled()) {
                return field.get();
            }
        }

        return nullptr;
    }

    void UiForm::set_clip(int32_t top, int32_t bottom) {
        clip = std::pair{top, bottom};
    }

    std::optional<size_t> UiForm::field_at(olc::vf2d point) const {
        float width = (float)RowField::width();

        if (clip && (point.y < clip->first || point.y > clip->second)) {
            return std::nullopt;
        }

        for (size_t index = 0; index < field_tops.size() && index < fields.size(); index++) {
            Rect area = {{(float)origin.x, (float)(origin.y + field_tops[index])}, {width, (float)(field_bottoms[index] - field_tops[index])}};

            if (fields[index]->focusable() && area.contains(point)) {
                return index;
            }
        }

        return std::nullopt;
    }

    bool UiForm::contains(olc::vf2d point) const {
        return field_at(point).has_value();
    }

    olc::vf2d UiForm::field_origin(size_t index) const {
        return {(float)origin.x, (float)(origin.y + (index < field_tops.size() ? field_tops[index] : 0))};
    }

    std::optional<UiForm::Action> UiForm::update_mouse(Input& input) {
        olc::vf2d mouse = input.mouse_position();

        if (dragging) {
            if (input.mouse(0).bHeld && *dragging < fields.size()) {
                if (fields[*dragging]->drag(mouse - field_origin(*dragging))) {
                    return Action::CHANGED;
                }
            }
            else {
                dragging.reset();
            }
        }

        if (!input.clicked(0)) {
            return std::nullopt;
        }

        std::optional<size_t> hit = field_at(mouse);

        if (!hit) {
            return std::nullopt;
        }

        input.take_click(0);
        bool moved = *hit != focus;

        if (moved) {
            focus_field(*hit);
        }

        dragging = *hit;

        if (fields[focus]->click(mouse - field_origin(focus), input)) {
            return Action::CHANGED;
        }

        return moved ? Action::MOVED : Action::NONE;
    }

    UiForm::Action UiForm::update(Input& input, float elapsed_time) {
        if (!enabled || fields.empty()) {
            return Action::NONE;
        }

        if (std::optional<Action> clicked = update_mouse(input)) {
            return *clicked;
        }

        bool changed = fields[focus]->update(input, elapsed_time);

        if (fields[focus]->consumes_navigation()) {
            return changed ? Action::CHANGED : Action::NONE;
        }

        bool down = down_repeat.update(input.pressed(actions::DOWN), input.held(actions::DOWN), elapsed_time);
        bool up = up_repeat.update(input.pressed(actions::UP), input.held(actions::UP), elapsed_time);

        if (int32_t wheel = input.mouse_wheel(); wheel != 0) {
            if (std::optional<size_t> next = step_focus(focus, wheel < 0, false)) {
                focus_field(*next);
                return Action::MOVED;
            }
        }

        if (!input.key_held(blocking_modifier) && input.take(actions::NEXT_FIELD)) {
            bool backwards = input.key_held(reverse_modifier);

            if (std::optional<size_t> next = step_focus(focus, !backwards, true)) {
                focus_field(*next);
                return Action::MOVED;
            }
        }

        if (input.take(actions::CONFIRM)) {
            if (std::optional<size_t> next = step_focus(focus, true, false)) {
                focus_field(*next);
                return Action::MOVED;
            }

            return Action::SUBMIT;
        }

        if (down) {
            input.consume(actions::DOWN);

            if (std::optional<size_t> next = step_focus(focus, true, false)) {
                focus_field(*next);
                return Action::MOVED;
            }
        }

        if (up) {
            input.consume(actions::UP);

            if (std::optional<size_t> previous = step_focus(focus, false, false)) {
                focus_field(*previous);
                return Action::MOVED;
            }
        }

        return changed ? Action::CHANGED : Action::NONE;
    }

    int32_t UiForm::draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position) const {
        int32_t y = position.y;
        origin = position;
        field_tops.assign(fields.size(), 0);
        field_bottoms.assign(fields.size(), 0);

        for (size_t i = 0; i < fields.size(); i++) {
            field_tops[i] = y - position.y;
            y += fields[i]->draw(window, theme, {position.x, y}, enabled && i == focus);
            field_bottoms[i] = y - position.y;
            y += Ui::px(spacing.value_or(Ui::layout("form.field_spacing")));
        }

        return y - position.y;
    }

    const UiField* UiForm::focused_field() const {
        return fields.empty() ? nullptr : fields[focus].get();
    }

    UiField* UiForm::focused_field() {
        return fields.empty() ? nullptr : fields[focus].get();
    }

    int32_t UiForm::focused_top() const {
        return focus < field_tops.size() ? field_tops[focus] : 0;
    }

    int32_t UiForm::focused_bottom() const {
        return focus < field_bottoms.size() ? field_bottoms[focus] : 0;
    }

    int32_t UiForm::height() const {
        return field_bottoms.empty() ? 0 : field_bottoms.back();
    }
}
