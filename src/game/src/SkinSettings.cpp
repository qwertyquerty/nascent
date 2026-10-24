#include "SkinSettings.h"

#include "Skin.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <cmath>

namespace nascent {
    double skin_setting(const Skin& skin, const SkinSetting& setting) {
        const std::optional<uint8_t>& chosen = setting.value(SettingsManager::settings);
        return chosen ? (double)*chosen : skin.layout(setting.key);
    }

    std::optional<double> lane_gap_setting() {
        const std::optional<uint8_t>& chosen = SettingsManager::settings.field_lane_gap;
        return chosen ? std::optional<double>(Ui::pxf(*chosen)) : std::nullopt;
    }

    double skin_lane_gap(const Skin& skin, uint8_t key_count) {
        double keys = std::max<double>(key_count, 1);
        double spacing = skin.manifest(key_count).key_spacing;
        double lane = Ui::REFERENCE_HEIGHT * skin_setting(skin, skin_settings::LANE_WIDTH) / 100.0;
        return keys * lane / (keys + keys * spacing - spacing) * spacing;
    }

    bool skin_toggle(const Skin& skin, const SkinToggle& toggle) {
        const std::optional<bool>& chosen = toggle.value(SettingsManager::settings);
        return chosen ? *chosen : skin.layout(toggle.key) != 0;
    }

    bool skin_toggle(const SkinToggle& toggle) {
        const std::optional<bool>& chosen = toggle.value(SettingsManager::settings);
        return chosen ? *chosen : Ui::layout(toggle.key) != 0;
    }

    olc::vf2d hud_position(const Skin& skin, HudElement element, olc::vf2d size, olc::vi2d screen) {
        const HudSlot& slot = HUD_SLOTS[(size_t)element];
        double x = std::clamp(skin_setting(skin, slot.x) / 100.0, 0.0, 1.0);
        double y = std::clamp(skin_setting(skin, slot.y) / 100.0, 0.0, 1.0);
        return {(float)std::round(x * std::max(0.0f, screen.x - size.x)), (float)std::round(y * std::max(0.0f, screen.y - size.y))};
    }
}
