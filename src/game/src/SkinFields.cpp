#include "SkinFields.h"

#include "Skin.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

namespace nascent {
    SkinSliderField::SkinSliderField(std::string label, SkinSetting setting, const Skin& skin, int32_t minimum, int32_t maximum, int32_t step, std::string unit)
        : SliderField(std::move(label), 0, minimum, maximum, step, std::move(unit)), setting(setting) {
        const std::optional<uint8_t>& saved = setting.value(SettingsManager::settings);
        follows_skin = !saved.has_value();
        refresh_skin(skin);

        if (saved) {
            value = std::clamp<int32_t>(*saved, minimum, maximum);
        }
    }

    std::string SkinSliderField::display_value(bool focused) const {
        return follows_skin ? std::format("Skin ({}{})", value, unit) : SliderField::display_value(focused);
    }

    void SkinSliderField::refresh_skin(const Skin& skin) {
        skin_value = std::clamp<int32_t>((int32_t)std::lround(skin.layout(setting.key)), minimum, maximum);

        if (follows_skin) {
            value = skin_value;
        }
    }

    bool SkinSliderField::use_skin_value() {
        if (follows_skin) {
            return false;
        }

        follows_skin = true;
        value = skin_value;
        return true;
    }

    void SkinSliderField::store(Settings& settings) const {
        setting.value(settings) = follows_skin ? std::nullopt : std::optional<uint8_t>((uint8_t)std::clamp<int32_t>(value, 0, 255));
    }

    bool SkinSliderField::update(Input& input, float elapsed_time) {
        if (input.key_pressed(olc::Key::DEL) && use_skin_value()) {
            return true;
        }

        bool changed = SliderField::update(input, elapsed_time);
        follows_skin = follows_skin && !changed;
        return changed;
    }

    bool SkinSliderField::click(olc::vf2d point, Input& input) {
        bool changed = SliderField::click(point, input);
        follows_skin = follows_skin && !changed;
        return changed;
    }

    bool SkinSliderField::drag(olc::vf2d point) {
        bool changed = SliderField::drag(point);
        follows_skin = follows_skin && !changed;
        return changed;
    }

    SkinChoiceField::SkinChoiceField(std::string label, SkinToggle toggle, const Skin& skin, std::string off_label, std::string on_label)
        : ChoiceField(std::move(label), {off_label, on_label}, 0), toggle(toggle) {
        plain_labels = {off_label, on_label};
        skin_labels = {std::format("Skin ({})", off_label), std::format("Skin ({})", on_label)};
        const std::optional<bool>& saved = toggle.value(SettingsManager::settings);
        follows_skin = !saved.has_value();
        refresh_skin(skin);

        if (saved) {
            selected = *saved ? 1 : 0;
        }

        show_labels();
    }

    void SkinChoiceField::show_labels() {
        options = follows_skin ? skin_labels : plain_labels;
    }

    void SkinChoiceField::refresh_skin(const Skin& skin) {
        skin_value = skin.layout(toggle.key) != 0;

        if (follows_skin) {
            selected = skin_value ? 1 : 0;
        }
    }

    bool SkinChoiceField::use_skin_value() {
        if (follows_skin) {
            return false;
        }

        follows_skin = true;
        selected = skin_value ? 1 : 0;
        show_labels();
        return true;
    }

    void SkinChoiceField::store(Settings& settings) const {
        toggle.value(settings) = follows_skin ? std::nullopt : std::optional<bool>(selected == 1);
    }

    bool SkinChoiceField::update(Input& input, float elapsed_time) {
        if (input.key_pressed(olc::Key::DEL) && use_skin_value()) {
            return true;
        }

        bool changed = ChoiceField::update(input, elapsed_time);

        if (changed && follows_skin) {
            follows_skin = false;
            show_labels();
        }

        return changed;
    }

    bool SkinChoiceField::click(olc::vf2d point, Input& input) {
        bool changed = ChoiceField::click(point, input);

        if (changed && follows_skin) {
            follows_skin = false;
            show_labels();
        }

        return changed;
    }
}
