#include "UiForm.h"

#include "InputManager.h"

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

    void UiForm::focus_field(olc::PixelGameEngine* window, size_t index) {
        if (fields.empty()) {
            return;
        }

        fields[focus]->on_blur(window);
        focus = std::min(index, fields.size() - 1);

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

        if (window->GetKey(InputManager::field_key).bPressed && !window->GetKey(InputManager::switch_modifier).bHeld && fields.size() > 1) {
            bool backwards = window->GetKey(InputManager::keybind_mod1).bHeld;
            focus_field(window, backwards ? (focus + fields.size() - 1) % fields.size() : (focus + 1) % fields.size());
            return Action::MOVED;
        }

        if (window->GetKey(InputManager::menu_start_key).bPressed) {
            if (focus + 1 < fields.size()) {
                focus_field(window, focus + 1);
                return Action::MOVED;
            }

            return Action::SUBMIT;
        }

        if (window->GetKey(InputManager::menu_down_key).bPressed && focus + 1 < fields.size()) {
            focus_field(window, focus + 1);
            return Action::MOVED;
        }

        if (window->GetKey(InputManager::menu_up_key).bPressed && focus > 0) {
            focus_field(window, focus - 1);
            return Action::MOVED;
        }

        return changed ? Action::CHANGED : Action::NONE;
    }

    int32_t UiForm::draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position) const {
        int32_t y = position.y;

        for (size_t i = 0; i < fields.size(); i++) {
            y += fields[i]->draw(window, skin, {position.x, y}, enabled && i == focus) + spacing;
        }

        return y - position.y;
    }
}
