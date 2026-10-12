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
    static constexpr int32_t OPTIONS_FIELD_SPACING = 12;
    static constexpr int32_t HEADER_HEIGHT = 156;
    static constexpr int32_t FOOTER_GAP = 40;
    static constexpr int32_t STATUS_OFFSET = 8;
    static constexpr int32_t SCROLL_MARGIN = 72;
    static constexpr float SCROLL_SPEED = 14.0f;
    static constexpr int32_t EDGE_FADE = 28;
    static constexpr int32_t SCROLLBAR_GAP = 24;
    static constexpr int32_t SCROLLBAR_WIDTH = 4;
    static constexpr uint8_t SCROLLBAR_TRACK_ALPHA = 24;
    static constexpr uint8_t SCROLLBAR_THUMB_ALPHA = 120;
    static constexpr std::array<uint16_t, 9> FRAME_RATE_OPTIONS = {0, 60, 120, 144, 165, 240, 360, 480, 1000};
    static constexpr std::array<std::pair<const char*, float>, 4> TRANSITION_OPTIONS = {{{"Off", 0.0f}, {"Fast", 0.1f}, {"Normal", 0.2f}, {"Slow", 0.35f}}};

    SceneOptions::SceneOptions(Skin* skin) : skin(skin) {}

    SceneOptions::~SceneOptions() = default;

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

        skins = Skin::available_skins();
        std::vector<std::string> skin_labels;
        size_t skin_index = 0;

        for (size_t index = 0; index < skins.size(); index++) {
            skin_labels.push_back(skins[index].name);

            if (skins[index].id == skin->get_id()) {
                skin_index = index;
            }
        }

        form.add<SectionField>("Display");
        skin_field = &form.add<ChoiceField>("Skin", skin_labels, skin_index);
        frame_rate_field = &form.add<ChoiceField>("Frame rate limit", frame_rate_labels, frame_rate_index);
        fps_counter_field = &form.add<ChoiceField>("FPS counter", std::vector<std::string>{"Off", "On"}, settings.show_fps ? 1 : 0);
        transition_field = &form.add<ChoiceField>("Scene transitions", transition_labels, transition_index);

        form.add<SectionField>("Audio");
        master_volume_field = &form.add<SliderField>("Master volume", settings.master_volume, 0, 100, VOLUME_STEP, "%");
        music_volume_field = &form.add<SliderField>("Music volume", settings.music_volume, 0, 100, VOLUME_STEP, "%");
        effects_volume_field = &form.add<SliderField>("Effects volume", settings.effects_volume, 0, 100, VOLUME_STEP, "%");

        form.add<SectionField>("Gameplay keys");

        for (size_t index = 0; index < KEY_BINDINGS.size(); index++) {
            key_fields.push_back(&form.add<KeyBindField>((KeyBinding)index));
        }

        form.spacing = OPTIONS_FIELD_SPACING;
        form.focus_field(game->window, 0);
    }

    void SceneOptions::apply() {
        Settings& settings = SettingsManager::settings;
        if (!skins.empty() && skins[skin_field->selected].id != skin->get_id()) {
            settings.skin = skins[skin_field->selected].id;
            skin->reload(settings.skin);
        }

        settings.master_volume = (uint8_t)master_volume_field->value;
        settings.music_volume = (uint8_t)music_volume_field->value;
        settings.effects_volume = (uint8_t)effects_volume_field->value;
        game->set_volumes(settings.master_volume / 100.0f, settings.music_volume / 100.0f, settings.effects_volume / 100.0f);
        settings.frame_rate_limit = frame_rates[frame_rate_field->selected];
        settings.show_fps = fps_counter_field->selected == 1;
        settings.scene_transition_time = TRANSITION_OPTIONS[transition_field->selected].second;
        game->scene_transition_time = settings.scene_transition_time;
        status.clear();

        for (KeyBindField* field : key_fields) {
            if (std::optional<KeyBinding> swapped = std::exchange(field->swapped, std::nullopt)) {
                status = std::format("{} was already on {}, it moved to {}", KEY_BINDINGS[(size_t)*swapped].label, InputManager::key_label(InputManager::get_key(field->binding)), InputManager::key_label(InputManager::get_key(*swapped)));
            }
        }

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
            game->play_sound(skin->sound(SkinSound::HITSOUND));
        }

        update_scroll(elapsed_time);
    }

    void SceneOptions::update_scroll(float elapsed_time) {
        int32_t view_height = viewport_bottom - viewport_top;

        if (view_height <= 0 || form.height() == 0) {
            return;
        }

        float max_scroll = (float)std::max(0, form.height() - view_height);
        float top = (float)(form.focused_top() - SCROLL_MARGIN);
        float bottom = (float)(form.focused_bottom() + SCROLL_MARGIN - view_height);

        scroll_target = std::clamp(std::clamp(scroll_target, bottom, std::max(top, bottom)), 0.0f, max_scroll);
        scroll += (scroll_target - scroll) * std::min(1.0f, elapsed_time * SCROLL_SPEED);
    }

    std::string SceneOptions::footer_text() const {
        const KeyBindField* key_field = dynamic_cast<const KeyBindField*>(form.focused_field());

        if (key_field != nullptr && key_field->is_capturing()) {
            return "Press the new key";
        }

        if (key_field != nullptr) {
            return "↑/↓:  choose        Enter:  rebind        Delete:  reset        Esc:  back";
        }

        return "↑/↓:  choose        ←/→:  change        Esc:  back";
    }

    void SceneOptions::draw_scrollbar(olc::PixelGameEngine* window, int32_t x) const {
        int32_t view_height = viewport_bottom - viewport_top;
        int32_t content_height = form.height();

        if (view_height <= 0 || content_height <= view_height) {
            return;
        }

        float track_x = (float)(x + RowField::WIDTH + SCROLLBAR_GAP);
        float thumb_height = (float)view_height * (float)view_height / (float)content_height;
        float thumb_top = (float)viewport_top + ((float)view_height - thumb_height) * (scroll / (float)(content_height - view_height));

        window->FillRectDecal({track_x, (float)viewport_top}, {(float)SCROLLBAR_WIDTH, (float)view_height}, olc::Pixel(255, 255, 255, SCROLLBAR_TRACK_ALPHA));
        window->FillRectDecal({track_x, thumb_top}, {(float)SCROLLBAR_WIDTH, thumb_height}, olc::Pixel(255, 255, 255, SCROLLBAR_THUMB_ALPHA));
    }

    void SceneOptions::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        std::string footer = footer_text();
        olc::FontRect footer_metrics = text_bounds(skin->display_font_small, "Hgjy");
        int32_t footer_bottom = screen.y - screen.y / 12;
        int32_t footer_top = footer_bottom - (footer_metrics.offset.y + footer_metrics.size.y);

        viewport_top = y + HEADER_HEIGHT;
        viewport_bottom = footer_top - FOOTER_GAP;
        form.draw(window, skin, {x, viewport_top - (int32_t)std::lround(scroll)});

        float max_scroll = (float)std::max(0, form.height() - (viewport_bottom - viewport_top));
        olc::Pixel clear = olc::Pixel(0, 0, 0, 0);
        window->FillRectDecal({0.0f, 0.0f}, {(float)screen.x, (float)viewport_top}, olc::BLACK);
        window->FillRectDecal({0.0f, (float)viewport_bottom}, {(float)screen.x, (float)(screen.y - viewport_bottom)}, olc::BLACK);

        if (scroll > 0.5f) {
            window->GradientFillRectDecal({0.0f, (float)viewport_top}, {(float)screen.x, (float)EDGE_FADE}, olc::BLACK, clear, clear, olc::BLACK);
        }

        if (scroll < max_scroll - 0.5f) {
            window->GradientFillRectDecal({0.0f, (float)(viewport_bottom - EDGE_FADE)}, {(float)screen.x, (float)EDGE_FADE}, clear, olc::BLACK, olc::BLACK, clear);
        }

        draw_scrollbar(window, x);

        draw_text(skin->display_font_large, "Options", {x, y}, olc::WHITE);
        y += 90;
        draw_text(skin->display_font_small, "Changes apply right away and are saved when you leave.", {x, y}, olc::GREY);

        if (!status.empty()) {
            draw_text(skin->display_font_small, status, {x, viewport_bottom + STATUS_OFFSET}, olc::GREY);
        }

        draw_text(skin->display_font_small, footer, {x, footer_top}, olc::GREY);
    }
}
