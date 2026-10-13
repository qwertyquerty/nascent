#include "UiForm.h"

#include "InputManager.h"
#include "Ui.h"

#include <algorithm>

namespace nascent {
    void UiForm::clear(olc::PixelGameEngine* window) {
        if (!fields.empty()) {
            fields[focus]->on_blur(window);
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

    void UiForm::focus_field(olc::PixelGameEngine* window, size_t index) {
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

        fields[focus]->on_blur(window);
        focus = index;

        if (enabled) {
            fields[focus]->on_focus(window);
        }
    }

    void UiForm::set_enabled(olc::PixelGameEngine* window, bool enabled) {
        this->enabled = enabled;

        if (fields.empty()) {
            return;
        }

        if (enabled) {
            fields[focus]->on_focus(window);
        }
        else {
            fields[focus]->on_blur(window);
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

    UiForm::Action UiForm::update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) {
        if (!enabled || fields.empty()) {
            return Action::NONE;
        }

        bool changed = fields[focus]->update(window, typed, elapsed_time);

        if (fields[focus]->consumes_navigation()) {
            return changed ? Action::CHANGED : Action::NONE;
        }

        bool down = down_repeat.update(window->GetKey(InputManager::menu_down_key), elapsed_time);
        bool up = up_repeat.update(window->GetKey(InputManager::menu_up_key), elapsed_time);

        if (int32_t wheel = window->GetMouseWheel(); wheel != 0) {
            if (std::optional<size_t> next = step_focus(focus, wheel < 0, false)) {
                focus_field(window, *next);
                return Action::MOVED;
            }
        }

        if (window->GetKey(InputManager::field_key).bPressed && !window->GetKey(InputManager::switch_modifier).bHeld) {
            bool backwards = window->GetKey(InputManager::keybind_mod1).bHeld;

            if (std::optional<size_t> next = step_focus(focus, !backwards, true)) {
                focus_field(window, *next);
                return Action::MOVED;
            }
        }

        if (window->GetKey(InputManager::menu_start_key).bPressed) {
            if (std::optional<size_t> next = step_focus(focus, true, false)) {
                focus_field(window, *next);
                return Action::MOVED;
            }

            return Action::SUBMIT;
        }

        if (down) {
            if (std::optional<size_t> next = step_focus(focus, true, false)) {
                focus_field(window, *next);
                return Action::MOVED;
            }
        }

        if (up) {
            if (std::optional<size_t> previous = step_focus(focus, false, false)) {
                focus_field(window, *previous);
                return Action::MOVED;
            }
        }

        return changed ? Action::CHANGED : Action::NONE;
    }

    int32_t UiForm::draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position) const {
        int32_t y = position.y;
        field_tops.assign(fields.size(), 0);
        field_bottoms.assign(fields.size(), 0);

        for (size_t i = 0; i < fields.size(); i++) {
            field_tops[i] = y - position.y;
            y += fields[i]->draw(window, skin, {position.x, y}, enabled && i == focus);
            field_bottoms[i] = y - position.y;
            y += Ui::px(spacing);
        }

        return y - position.y;
    }

    const UiField* UiForm::focused_field() const {
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
