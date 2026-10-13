#include "SceneOptions.h"
#include "RichPresence.h"

#include "Account.h"
#include "Attempt.h"
#include "Chart.h"
#include "EntityField.h"
#include "Game.h"
#include "RowField.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "TextCache.h"
#include "TextField.h"
#include "TimingBar.h"
#include "Ui.h"

#include <iostream>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace nascent {
    static constexpr int32_t PERCENT_STEP = 1;
    static constexpr int32_t OPTIONS_FIELD_SPACING = 12;
    static constexpr int32_t LANE_WIDTH_MIN = 6;
    static constexpr int32_t LANE_WIDTH_MAX = 18;
    static constexpr int16_t LANE_GAP_MAX = 48;
    static constexpr int16_t LANE_GAP_STEP = 4;
    static constexpr int32_t JUDGE_LINE_MAX = 37;
    static constexpr int32_t TIMING_BAR_LENGTH_MIN = 10;
    static constexpr int32_t TIMING_BAR_WIDTH_MIN = 1;
    static constexpr int32_t TIMING_BAR_WIDTH_MAX = 20;
    static constexpr uint8_t PREVIEW_KEY_COUNT = 4;
    static constexpr int32_t PREVIEW_BEAT_MS = 250;
    static constexpr int32_t PREVIEW_LOOP_BEATS = 32;
    static constexpr int32_t PREVIEW_HOLD_BEATS = 2;
    static constexpr int32_t PREVIEW_HOLD_EVERY = 8;
    static constexpr int32_t PREVIEW_TAP_MS = 60;
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
    static constexpr float SERVER_CHECK_DELAY = 0.6f;
    static constexpr std::array<std::pair<const char*, float>, 4> TRANSITION_OPTIONS = {{{"Off", 0.0f}, {"Fast", 0.1f}, {"Normal", 0.2f}, {"Slow", 0.35f}}};

    static uint32_t autoplay_keys(const Chart& chart, double time) {
        uint32_t keys = chart.held_lanes_at(time);

        for (const Hit& hit : chart.hits) {
            if (hit.time > time) {
                break;
            }

            if (hit.hit_type == HitType::HIT && time - hit.time < PREVIEW_TAP_MS) {
                keys |= 1u << hit.key;
            }
        }

        return keys;
    }

    SceneOptions::SceneOptions(Skin* skin) : skin(skin) {}

    SceneOptions::~SceneOptions() = default;

    void SceneOptions::init(Game* game) {
        RichPresence::in_menu("Changing settings");
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
        rich_presence_field = &form.add<ChoiceField>("Discord rich presence", std::vector<std::string>{"Off", "On"}, settings.discord_rich_presence ? 1 : 0);
        transition_field = &form.add<ChoiceField>("Scene transitions", transition_labels, transition_index);
        fullscreen_field = &form.add<ChoiceField>("Fullscreen (applies on restart)", std::vector<std::string>{"Off", "On"}, settings.display_fullscreen ? 1 : 0);

        form.add<SectionField>("Audio");
        master_volume_field = &form.add<SliderField>("Master volume", settings.master_volume, 0, 100, PERCENT_STEP, "%");
        music_volume_field = &form.add<SliderField>("Music volume", settings.music_volume, 0, 100, PERCENT_STEP, "%");
        effects_volume_field = &form.add<SliderField>("Effects volume", settings.effects_volume, 0, 100, PERCENT_STEP, "%");
        hitsounds_field = &form.add<ChoiceField>("Hitsounds", std::vector<std::string>{"Off", "On"}, settings.field_hitsounds ? 1 : 0);

        form.add<SectionField>("Playfield");
        lane_width_field = &form.add<SliderField>("Lane width", settings.field_lane_width_percent, LANE_WIDTH_MIN, LANE_WIDTH_MAX, PERCENT_STEP, "%");

        lane_gaps = {-1};

        for (int16_t gap = 0; gap <= LANE_GAP_MAX; gap += LANE_GAP_STEP) {
            lane_gaps.push_back(gap);
        }

        if (std::find(lane_gaps.begin(), lane_gaps.end(), settings.field_lane_gap) == lane_gaps.end()) {
            lane_gaps.push_back(settings.field_lane_gap);
            std::sort(lane_gaps.begin(), lane_gaps.end());
        }

        std::vector<std::string> lane_gap_labels;

        for (int16_t gap : lane_gaps) {
            lane_gap_labels.push_back(gap < 0 ? std::string("Skin") : std::format("{} px", gap));
        }

        size_t lane_gap_index = (size_t)(std::find(lane_gaps.begin(), lane_gaps.end(), settings.field_lane_gap) - lane_gaps.begin());
        lane_gap_field = &form.add<ChoiceField>("Lane separation", lane_gap_labels, lane_gap_index);
        judge_offset_field = &form.add<SliderField>("Judgement line height", settings.field_judge_line_percent, 0, JUDGE_LINE_MAX, PERCENT_STEP, "%");
        scroll_direction_field = &form.add<ChoiceField>("Scroll direction", std::vector<std::string>{"Down", "Up"}, settings.field_upscroll ? 1 : 0);
        mirror_field = &form.add<ChoiceField>("Mirror", std::vector<std::string>{"Off", "On"}, settings.field_mirror ? 1 : 0);
        judgement_field = &form.add<ChoiceField>("Judgement", std::vector<std::string>{"Hidden", "Shown"}, settings.field_show_judgement ? 1 : 0);
        judgement_height_field = &form.add<SliderField>("Judgement height", settings.field_judgement_height, 0, 100, PERCENT_STEP, "%");
        combo_field = &form.add<ChoiceField>("Combo", std::vector<std::string>{"Hidden", "Shown"}, settings.field_show_combo ? 1 : 0);
        combo_height_field = &form.add<SliderField>("Combo height", settings.field_combo_height, 0, 100, PERCENT_STEP, "%");
        timing_bar_field = &form.add<ChoiceField>("Hit timing bar", std::vector<std::string>{"Hidden", "Shown"}, settings.field_show_timing_bar ? 1 : 0);
        timing_bar_height_field = &form.add<SliderField>("Hit timing bar height", settings.field_timing_bar_height, 0, 100, PERCENT_STEP, "%");
        timing_bar_length_field = &form.add<SliderField>("Hit timing bar length", settings.field_timing_bar_length, TIMING_BAR_LENGTH_MIN, TIMING_BAR_MAX_LENGTH, PERCENT_STEP, "%");
        timing_bar_width_field = &form.add<SliderField>("Hit timing bar width", settings.field_timing_bar_width, TIMING_BAR_WIDTH_MIN, TIMING_BAR_WIDTH_MAX, PERCENT_STEP, "%");

        form.add<SectionField>("Gameplay keys");

        for (size_t index = 0; index < KEY_BINDINGS.size(); index++) {
            key_fields.push_back(&form.add<KeyBindField>((KeyBinding)index));
        }

        form.add<SectionField>("Online");
        server_field = &form.add<TextField>("Server address", settings.server_url);
        server_typed = settings.server_url;
        server_checked = settings.server_url;

        form.spacing = OPTIONS_FIELD_SPACING;
        form.focus_field(game->window, 0);

        build_preview_chart();
        rebuild_preview();
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
        game->set_volumes(SettingsManager::volume_gain(settings.master_volume), SettingsManager::volume_gain(settings.music_volume), SettingsManager::volume_gain(settings.effects_volume));
        settings.frame_rate_limit = frame_rates[frame_rate_field->selected];
        settings.show_fps = fps_counter_field->selected == 1;
        settings.discord_rich_presence = rich_presence_field->selected == 1;
        RichPresence::set_enabled(settings.discord_rich_presence);
        settings.scene_transition_time = TRANSITION_OPTIONS[transition_field->selected].second;
        game->scene_transition_time = settings.scene_transition_time;
        settings.display_fullscreen = fullscreen_field->selected == 1;
        settings.field_hitsounds = hitsounds_field->selected == 1;
        settings.field_mirror = mirror_field->selected == 1;
        settings.field_lane_width_percent = (uint8_t)lane_width_field->value;
        settings.field_lane_gap = lane_gaps[lane_gap_field->selected];
        settings.field_judge_line_percent = (uint8_t)judge_offset_field->value;
        settings.field_upscroll = scroll_direction_field->selected == 1;
        settings.field_show_judgement = judgement_field->selected == 1;
        settings.field_judgement_height = (uint8_t)judgement_height_field->value;
        settings.field_show_combo = combo_field->selected == 1;
        settings.field_combo_height = (uint8_t)combo_height_field->value;
        settings.field_show_timing_bar = timing_bar_field->selected == 1;
        settings.field_timing_bar_height = (uint8_t)timing_bar_height_field->value;
        settings.field_timing_bar_length = (uint8_t)timing_bar_length_field->value;
        settings.field_timing_bar_width = (uint8_t)timing_bar_width_field->value;
        rebuild_preview();
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
        commit_server();

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

        update_server(elapsed_time);
        update_scroll(elapsed_time);
        update_preview(elapsed_time);
    }

    void SceneOptions::update_server(float elapsed_time) {
        if (server_field == nullptr) {
            return;
        }

        if (server_field->value != server_typed) {
            server_typed = server_field->value;
            server_check_delay = SERVER_CHECK_DELAY;
        }

        server_check_delay = std::max(0.0f, server_check_delay - elapsed_time);
        std::string url = server_host(server_typed);

        if (server_check.valid() && server_check.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ApiResult<ServerHealth> result = server_check.get();
            server_checked = server_checking;

            if (server_checked == url) {
                if (!result.ok()) {
                    game->notify(std::format("Couldn't reach {}: {}", server_checked, result.error()), true);
                }
                else if (!result.value->healthy) {
                    game->notify(std::format("{} answered but isn't healthy right now", server_checked), true);
                }
                else {
                    game->notify(std::format("{} is online ({:.0f} ms)", server_checked, result.value->latency_ms));
                }
            }
        }

        if (!server_check.valid() && server_check_delay <= 0 && !url.empty() && url != server_checked) {
            server_checking = url;
            server_check = ServerApi::health(url);
        }

        if (form.focused_field() != server_field) {
            commit_server();
        }
    }

    void SceneOptions::commit_server() {
        if (server_field == nullptr) {
            return;
        }

        Settings& settings = SettingsManager::settings;
        std::string url = server_host(server_field->value);

        if (url.empty()) {
            url = settings.server_url;
        }

        server_field->value = url;
        server_typed = url;

        if (url == settings.server_url) {
            return;
        }

        settings.server_url = url;
        Account::use_server(url);
        changed = true;
    }

    void SceneOptions::build_preview_chart() {
        ChartInfo info;
        info.title = "Options Preview";
        info.key_count = PREVIEW_KEY_COUNT;
        info.hit_accuracy = MAX_HIT_ACC_DIFFICULTY;

        std::vector<Hit> hits;

        for (int32_t beat = 0; beat < PREVIEW_LOOP_BEATS * 2; beat++) {
            uint8_t lane = (uint8_t)(beat % PREVIEW_KEY_COUNT);
            int32_t time = beat * PREVIEW_BEAT_MS;

            if (beat % PREVIEW_HOLD_EVERY == PREVIEW_KEY_COUNT / 2) {
                hits.push_back({lane, HitType::HOLD, time, time + PREVIEW_HOLD_BEATS * PREVIEW_BEAT_MS});
            }
            else {
                hits.push_back({lane, HitType::HIT, time, time});
            }
        }

        std::string error;
        preview_chart = Chart::create(info, std::move(hits), {{0, (float)PREVIEW_BEAT_MS, 4}}, error);

        if (preview_chart == nullptr) {
            std::cerr << "Options preview: " << error << std::endl;
        }
    }

    void SceneOptions::rebuild_preview() {
        if (preview_chart == nullptr) {
            return;
        }

        const Settings& settings = SettingsManager::settings;
        olc::vi2d screen = game->window->GetScreenSize();
        double width = screen.y * settings.field_lane_width_percent / 100.0 * PREVIEW_KEY_COUNT;

        preview_field = std::make_unique<EntityField>(preview_chart.get(), skin, olc::vd2d{(double)screen.x * 3 / 4 - width / 2, 0.0}, olc::vd2d{width, (double)screen.y});
        preview_field->draw_judge_colors = true;
        preview_field->draw_notes_past_judge = false;
        preview_field->draw_hit_score = settings.field_show_judgement;
        preview_field->draw_combo = settings.field_show_combo;
        preview_field->judgement_height = settings.field_judgement_height / 100.0;
        preview_field->combo_height = settings.field_combo_height / 100.0;
        preview_field->set_attempt(preview_attempt.get());
        preview_field->judge_alpha = 64;
        preview_field->judge_height = screen.y * settings.field_judge_line_percent / 100.0;
        preview_field->upscroll = settings.field_upscroll;
        preview_field->mirror = settings.field_mirror;
        preview_field->set_lane_gap(settings.field_lane_gap >= 0 ? std::optional<double>(Ui::pxf(settings.field_lane_gap)) : std::nullopt);
    }

    void SceneOptions::on_resize() {
        rebuild_preview();
    }

    void SceneOptions::update_preview(float elapsed_time) {
        if (preview_field == nullptr) {
            return;
        }

        preview_time += elapsed_time;
        double position = std::fmod(preview_time * 1000.0, (double)PREVIEW_LOOP_BEATS * PREVIEW_BEAT_MS);

        double previous = preview_position;

        if (preview_attempt == nullptr || position < preview_position) {
            preview_attempt = std::make_unique<Attempt>(preview_chart.get());
            preview_field->set_attempt(preview_attempt.get());
            previous = -1;
        }

        std::vector<double> moments;

        for (const Hit& hit : preview_chart->hits) {
            for (double moment : {(double)hit.time, (double)(hit.hit_type == HitType::HOLD ? hit.end_time : hit.time + PREVIEW_TAP_MS)}) {
                if (moment > previous && moment <= position) {
                    moments.push_back(moment);
                }
            }
        }

        std::sort(moments.begin(), moments.end());

        for (double moment : moments) {
            preview_attempt->set_keys(autoplay_keys(*preview_chart, moment), (int32_t)moment);
        }

        preview_position = position;
        uint32_t keys = autoplay_keys(*preview_chart, position);
        preview_attempt->set_keys(keys, (int32_t)position);
        preview_field->set_position(position);
        preview_field->set_keys(keys);
    }

    void SceneOptions::update_scroll(float elapsed_time) {
        int32_t view_height = viewport_bottom - viewport_top;

        if (view_height <= 0 || form.height() == 0) {
            return;
        }

        float max_scroll = (float)std::max(0, form.height() - view_height);
        float top = (float)(form.focused_top() - Ui::px(SCROLL_MARGIN));
        float bottom = (float)(form.focused_bottom() + Ui::px(SCROLL_MARGIN) - view_height);

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

        if (server_field != nullptr && form.focused_field() == server_field) {
            return "Type an address        ↑/↓:  choose        Esc:  save and go back";
        }

        return "↑/↓:  choose        ←/→:  change        Esc:  back";
    }

    void SceneOptions::draw_scrollbar(olc::PixelGameEngine* window, int32_t x) const {
        int32_t view_height = viewport_bottom - viewport_top;
        int32_t content_height = form.height();

        if (view_height <= 0 || content_height <= view_height) {
            return;
        }

        float track_x = (float)(x + RowField::width() + Ui::px(SCROLLBAR_GAP));
        float track_width = std::max(1.0f, Ui::pxf(SCROLLBAR_WIDTH));
        float thumb_height = (float)view_height * (float)view_height / (float)content_height;
        float thumb_top = (float)viewport_top + ((float)view_height - thumb_height) * (scroll / (float)(content_height - view_height));

        window->FillRectDecal({track_x, (float)viewport_top}, {track_width, (float)view_height}, olc::Pixel(255, 255, 255, SCROLLBAR_TRACK_ALPHA));
        window->FillRectDecal({track_x, thumb_top}, {track_width, thumb_height}, olc::Pixel(255, 255, 255, SCROLLBAR_THUMB_ALPHA));
    }

    void SceneOptions::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        std::string footer = footer_text();
        olc::FontRect footer_metrics = text_bounds(skin->display_font_small, "Hgjy");
        int32_t footer_bottom = screen.y - screen.y / 12;
        int32_t footer_top = footer_bottom - (footer_metrics.offset.y + footer_metrics.size.y);

        viewport_top = y + Ui::px(HEADER_HEIGHT);
        viewport_bottom = footer_top - Ui::px(FOOTER_GAP);
        float edge_fade = Ui::pxf(EDGE_FADE);
        form.draw(window, skin, {x, viewport_top - (int32_t)std::lround(scroll)});

        float max_scroll = (float)std::max(0, form.height() - (viewport_bottom - viewport_top));
        olc::Pixel clear = olc::Pixel(0, 0, 0, 0);
        window->FillRectDecal({0.0f, 0.0f}, {(float)screen.x, (float)viewport_top}, olc::BLACK);
        window->FillRectDecal({0.0f, (float)viewport_bottom}, {(float)screen.x, (float)(screen.y - viewport_bottom)}, olc::BLACK);

        if (scroll > 0.5f) {
            window->GradientFillRectDecal({0.0f, (float)viewport_top}, {(float)screen.x, edge_fade}, olc::BLACK, clear, clear, olc::BLACK);
        }

        if (scroll < max_scroll - 0.5f) {
            window->GradientFillRectDecal({0.0f, (float)viewport_bottom - edge_fade}, {(float)screen.x, edge_fade}, clear, olc::BLACK, olc::BLACK, clear);
        }

        draw_scrollbar(window, x);

        if (preview_field != nullptr) {
            preview_field->draw(window);

            if (SettingsManager::settings.field_show_timing_bar && preview_attempt != nullptr) {
                const Settings& settings = SettingsManager::settings;
                draw_timing_bar(window, *preview_attempt, screen.x * 3 / 4.0f, screen.x * settings.field_timing_bar_length / 100.0f, screen.y * settings.field_timing_bar_width / 100.0f, settings.field_timing_bar_height / 100.0f);
            }
        }

        draw_text(skin->display_font_large, "Options", {x, y}, olc::WHITE);
        y += Ui::px(90);
        draw_text(skin->display_font_small, "Changes apply right away and are saved when you leave.", {x, y}, olc::GREY);

        if (!status.empty()) {
            draw_text(skin->display_font_small, status, {x, viewport_bottom + Ui::px(STATUS_OFFSET)}, olc::GREY);
        }

        draw_text(skin->display_font_small, footer, {x, footer_top}, olc::GREY);
    }
}
