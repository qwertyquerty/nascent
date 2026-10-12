#include "SceneOptions.h"

#include "Game.h"
#include "RowField.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "TextCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace nascent {
    static constexpr int32_t VOLUME_STEP = 5;
    static constexpr std::array<uint16_t, 9> FRAME_RATE_OPTIONS = {0, 60, 120, 144, 165, 240, 360, 480, 1000};
    static constexpr std::array<std::pair<const char*, float>, 4> TRANSITION_OPTIONS = {{{"Off", 0.0f}, {"Fast", 0.1f}, {"Normal", 0.2f}, {"Slow", 0.35f}}};

    SceneOptions::SceneOptions(Skin* skin) : skin(skin) {}

    void SceneOptions::init(Game* game) {
        const Settings& settings = SettingsManager::settings;

        frame_rates.assign(FRAME_RATE_OPTIONS.begin(), FRAME_RATE_OPTIONS.end());

        if (std::find(frame_rates.begin(), frame_rates.end(), settings.frame_rate_limit) == frame_rates.end()) {
            frame_rates.push_back(settings.frame_rate_limit);
            std::sort(frame_rates.begin(), frame_rates.end());
        }

        std::vector<std::string> frame_rate_labels;

        for (uint16_t rate : frame_rates) {
            frame_rate_labels.push_back(rate == 0 ? std::string("Unlimited") : std::format("{} fps", rate));
        }

        size_t frame_rate_index = (size_t)(std::find(frame_rates.begin(), frame_rates.end(), settings.frame_rate_limit) - frame_rates.begin());

        std::vector<std::string> transition_labels;
        size_t transition_index = 0;

        for (size_t index = 0; index < TRANSITION_OPTIONS.size(); index++) {
            transition_labels.push_back(TRANSITION_OPTIONS[index].first);

            if (std::abs(TRANSITION_OPTIONS[index].second - settings.scene_transition_time) < std::abs(TRANSITION_OPTIONS[transition_index].second - settings.scene_transition_time)) {
                transition_index = index;
            }
        }

        master_volume_field = &form.add<SliderField>("Master volume", settings.master_volume, 0, 100, VOLUME_STEP, "%");
        music_volume_field = &form.add<SliderField>("Music volume", settings.music_volume, 0, 100, VOLUME_STEP, "%");
        effects_volume_field = &form.add<SliderField>("Effects volume", settings.effects_volume, 0, 100, VOLUME_STEP, "%");
        frame_rate_field = &form.add<ChoiceField>("Frame rate limit", frame_rate_labels, frame_rate_index);
        fps_counter_field = &form.add<ChoiceField>("FPS counter", std::vector<std::string>{"Off", "On"}, settings.show_fps ? 1 : 0);
        transition_field = &form.add<ChoiceField>("Scene transitions", transition_labels, transition_index);
        form.focus_field(game->window, 0);
    }

    void SceneOptions::apply() {
        Settings& settings = SettingsManager::settings;
        settings.master_volume = (uint8_t)master_volume_field->value;
        settings.music_volume = (uint8_t)music_volume_field->value;
        settings.effects_volume = (uint8_t)effects_volume_field->value;
        game->set_volumes(settings.master_volume / 100.0f, settings.music_volume / 100.0f, settings.effects_volume / 100.0f);
        settings.frame_rate_limit = frame_rates[frame_rate_field->selected];
        settings.show_fps = fps_counter_field->selected == 1;
        settings.scene_transition_time = TRANSITION_OPTIONS[transition_field->selected].second;
        game->scene_transition_time = settings.scene_transition_time;
        changed = true;
    }

    void SceneOptions::on_exit() {
        form.set_enabled(game->window, false);

        if (std::exchange(changed, false)) {
            SettingsManager::save();
        }
    }

    void SceneOptions::update(float elapsed_time) {
        UiForm::Action action = form.update(game->window, game->get_frame_text(), elapsed_time);

        if (action == UiForm::Action::CHANGED) {
            apply();
        }

        if (action == UiForm::Action::MOVED || action == UiForm::Action::CHANGED) {
            game->play_sound(skin->hitsound_sfx);
        }
    }

    void SceneOptions::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_large, "Options", {x, y}, olc::WHITE);
        y += 90;
        draw_text(skin->display_font_small, "Changes apply right away and are saved when you leave.", {x, y}, olc::GREY);
        y += 26;
        draw_text(skin->display_font_small, "Offsets and scroll speed are in calibration (F3), and lane keys are in input (F6), from the song list.", {x, y}, olc::DARK_GREY);
        y += 60;

        form.draw(window, skin, {x, y});

        std::string footer = "↑/↓:  choose        ←/→:  change        Esc:  back";
        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, footer);
        int32_t footer_bottom = screen.y - screen.y / 12;
        draw_text(skin->display_font_small, footer, {x, footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y)}, olc::GREY);
    }
}
