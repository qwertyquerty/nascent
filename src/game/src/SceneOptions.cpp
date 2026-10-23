#include "SceneOptions.h"
#include "RichPresence.h"
#include "SceneCalibration.h"
#include "SceneCredits.h"

#include "Account.h"
#include "Attempt.h"
#include "Chart.h"
#include "ChartSearch.h"
#include "ChangelogOverlay.h"
#include "ChartFolders.h"
#include "EtternaSkin.h"
#include "EntityField.h"
#include "gse/Game.h"
#include "gse/Platform.h"
#include "gse/ui/RowField.h"
#include "KeyBindField.h"
#include "ServerClient.h"
#include "SettingsManager.h"
#include "SkinFields.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/TextField.h"
#include "TimingBar.h"
#include "gse/ui/Ui.h"
#include "gse/render/Color.h"
#include "gse/concurrency/Async.h"

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
    static constexpr const char* DELETE_SKIN_LABEL = "Delete skin";
    static constexpr const char* SKIN_EXPORT_FOLDER = "exports";
    static constexpr const char* BUG_REPORT_URL = "https://github.com/qwertyquerty/nascent/issues/new";
    static constexpr const char* DISCORD_URL = "https://discord.gg/f538azJwXT";
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

    void SceneOptions::on_create() {
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
        skin_list_version = Skin::list_version();
        std::vector<std::string> skin_labels;
        size_t skin_index = 0;

        for (size_t index = 0; index < skins.size(); index++) {
            skin_labels.push_back(skins[index].name);

            if (skins[index].id == skin->get_id()) {
                skin_index = index;
            }
        }

        skin_fields.clear();
        visibility_fields.clear();
        position_fields.clear();
        form.add<SectionField>("Window");
        if (window_placement_supported()) {
            fullscreen_field = &form.add<ChoiceField>("Display mode", std::vector<std::string>{"Windowed", "Fullscreen", "Borderless window"}, (size_t)SettingsManager::window_mode());
            monitors.clear();
            std::vector<std::string> monitor_labels;
            size_t primary = 0;

            for (const DisplayInfo& display : list_displays()) {
                size_t digits = display.id.find_last_not_of("0123456789") + 1;
                std::string number = digits < display.id.size() ? display.id.substr(digits) : std::to_string(monitors.size() + 1);
                std::string name = display.name.empty() ? std::string("Monitor") : display.name;
                primary = display.primary ? monitors.size() : primary;
                monitor_labels.push_back(std::format("{}: {} ({}x{}){}", number, name, display.width, display.height, display.primary ? ", main" : ""));
                monitors.push_back(display.id);
            }

            auto saved_monitor = std::find(monitors.begin(), monitors.end(), settings.display_monitor);

            if (saved_monitor == monitors.end() && !settings.display_monitor.empty()) {
                monitor_labels.push_back("Saved monitor (not connected)");
                monitors.push_back(settings.display_monitor);
                saved_monitor = monitors.end() - 1;
            }

            if (!monitors.empty()) {
                monitor_shown = saved_monitor != monitors.end() ? (size_t)(saved_monitor - monitors.begin()) : primary;
                monitor_field = &form.add<ChoiceField>("Fullscreen monitor", monitor_labels, monitor_shown);
            }
        }
        else {
            fullscreen_field = &form.add<ChoiceField>("Fullscreen (applies on restart)", std::vector<std::string>{"Off", "On"}, settings.display_fullscreen ? 1 : 0);
        }

        frame_rate_field = &form.add<ChoiceField>("Frame rate limit", frame_rate_labels, frame_rate_index);
        transition_field = &form.add<ChoiceField>("Scene transitions", transition_labels, transition_index);

        form.add<SectionField>("Gameplay");
        scroll_speed_field = &form.add<SliderField>("Scroll speed", settings.field_scroll_speed, SceneCalibration::SCROLL_SPEED_MIN, SceneCalibration::SCROLL_SPEED_MAX, SceneCalibration::SCROLL_SPEED_STEP, "");
        scroll_direction_field = &form.add<ChoiceField>("Scroll direction", std::vector<std::string>{"Down", "Up"}, settings.field_upscroll ? 1 : 0);
        mirror_field = &form.add<ChoiceField>("Mirror", std::vector<std::string>{"Off", "On"}, settings.field_mirror ? 1 : 0);
        lane_cover_field = &form.add<SliderField>("Noteblock", settings.field_lane_cover, -100, 100, PERCENT_STEP, "%");
        instant_restart_field = &form.add<ChoiceField>("Instant restart", std::vector<std::string>{"Off", "On"}, settings.field_instant_restart ? 1 : 0);

        form.add<SectionField>("Audio");
        master_volume_field = &form.add<SliderField>("Master volume", settings.master_volume, 0, 100, PERCENT_STEP, "%");
        music_volume_field = &form.add<SliderField>("Music volume", settings.music_volume, 0, 100, PERCENT_STEP, "%");
        effects_volume_field = &form.add<SliderField>("Effects volume", settings.effects_volume, 0, 100, PERCENT_STEP, "%");
        hitsounds_field = &form.add<ChoiceField>("Hitsounds", std::vector<std::string>{"Off", "On"}, settings.field_hitsounds ? 1 : 0);
        output_devices = {std::string()};
        std::vector<std::string> device_labels = {"System default"};

        for (std::string& name : game->audio().engine().GetPlaybackDeviceNames()) {
            device_labels.push_back(name);
            output_devices.push_back(std::move(name));
        }

        auto saved_device = std::find(output_devices.begin(), output_devices.end(), settings.audio_output_device);

        if (saved_device == output_devices.end()) {
            device_labels.push_back(std::format("{} (not connected)", settings.audio_output_device));
            output_devices.push_back(settings.audio_output_device);
            saved_device = output_devices.end() - 1;
        }

        output_device_field = &form.add<ChoiceField>("Output device", device_labels, (size_t)(saved_device - output_devices.begin()));

        title_music_field = &form.add<ChoiceField>("Title screen songs", std::vector<std::string>{"All", "Favorites", "None"}, std::min<size_t>((size_t)settings.title_songs, 2));

        form.add<SectionField>("Calibration");
        visual_offset_field = &form.add<SliderField>("Visual offset", settings.field_audio_visual_offset, -SceneCalibration::OFFSET_LIMIT_MS, SceneCalibration::OFFSET_LIMIT_MS, 1, " ms");
        input_offset_field = &form.add<SliderField>("Input offset", settings.field_audio_input_offset, -SceneCalibration::OFFSET_LIMIT_MS, SceneCalibration::OFFSET_LIMIT_MS, 1, " ms");
        calibration_button = &form.add<ButtonField>("Open calibration");

        form.add<SectionField>("Skin");
        skin_field = &form.add<ChoiceField>("Skin", skin_labels, skin_index);
        export_skin_button = &form.add<ButtonField>("Export skin");
        delete_skin_button = &form.add<ButtonField>(DELETE_SKIN_LABEL);
        note_gradient_field = &form.add<ChoiceField>("Note gradient", std::vector<std::string>{"Off", "On"}, settings.field_note_gradient ? 1 : 0);
        note_opacity_field = &form.add<SliderField>("Note opacity", settings.field_note_opacity, 0, 100, PERCENT_STEP, "%");
        receptor_opacity_field = &form.add<SliderField>("Receptor opacity", settings.field_receptor_opacity, 0, 100, PERCENT_STEP, "%");
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
        background_brightness_field = &form.add<SliderField>("Background brightness", settings.field_background_brightness, 0, 100, PERCENT_STEP, "%");

        form.add<SectionField>("Element Visibility");
        reset_visibility_button = &form.add<ButtonField>("Reset all to skin default");
        add_skin_toggle("Judgement line", skin_toggles::SHOW_JUDGEMENT_LINE);
        add_skin_toggle("Judgement", skin_toggles::SHOW_JUDGEMENT);
        add_skin_toggle("Combo", skin_toggles::SHOW_COMBO);
        add_skin_toggle("Accuracy", skin_toggles::SHOW_ACCURACY);
        add_skin_toggle("Average error", skin_toggles::SHOW_AVERAGE_ERROR);
        add_skin_toggle("Error deviation", skin_toggles::SHOW_ERROR_DEVIATION);
        add_skin_toggle("Performance prediction", skin_toggles::SHOW_PERFORMANCE);
        add_skin_toggle("Judgement counter", skin_toggles::SHOW_JUDGEMENT_COUNTER);
        add_skin_toggle("Hit timing bar", skin_toggles::SHOW_TIMING_BAR);
        add_skin_toggle("Hit timing average", skin_toggles::SHOW_TIMING_AVERAGE);
        add_skin_toggle("FPS counter", skin_toggles::SHOW_FPS, "Off", "On");

        form.add<SectionField>("Element Positions");
        reset_positions_button = &form.add<ButtonField>("Reset all to skin default");
        add_skin_slider("Lane width", skin_settings::LANE_WIDTH, LANE_WIDTH_MIN, LANE_WIDTH_MAX);
        add_skin_slider("Judgement line height", skin_settings::JUDGEMENT_LINE_HEIGHT, 0, JUDGE_LINE_MAX);
        add_skin_slider("Judgement height", skin_settings::JUDGEMENT_HEIGHT, 0, 100);
        add_skin_slider("Combo height", skin_settings::COMBO_HEIGHT, 0, 100);
        add_hud_position(HudElement::ACCURACY);
        add_hud_position(HudElement::AVERAGE_ERROR);
        add_hud_position(HudElement::PERFORMANCE);
        add_hud_position(HudElement::JUDGEMENT_COUNTER);
        add_skin_slider("Hit timing bar height", skin_settings::TIMING_BAR_HEIGHT, 0, 100);
        add_skin_slider("Hit timing bar length", skin_settings::TIMING_BAR_LENGTH, TIMING_BAR_LENGTH_MIN, TIMING_BAR_MAX_LENGTH);
        add_skin_slider("Hit timing bar width", skin_settings::TIMING_BAR_WIDTH, TIMING_BAR_WIDTH_MIN, TIMING_BAR_WIDTH_MAX);
        add_hud_position(HudElement::PROGRESS);
        add_hud_position(HudElement::FPS);

        form.add<SectionField>("Keys");
        for (size_t index = 0; index < KEY_BINDINGS.size(); index++) {
            key_fields.push_back(&form.add<KeyBindField>((KeyBinding)index));
        }


        form.add<SectionField>("Online");
        server_field = &form.add<TextField>("Server address", settings.server_url);
        server_typed = settings.server_url;
        server_checked = settings.server_url;

        rich_presence_field = &form.add<ChoiceField>("Discord rich presence", std::vector<std::string>{"Off", "On"}, settings.discord_rich_presence ? 1 : 0);

        form.add<SectionField>("Library");
        link_folder_button = &form.add<ButtonField>("Link a charts folder");
        osu_songs = ChartFolders::osu_songs_folder();

        if (osu_songs) {
            link_osu_button = &form.add<ButtonField>("Link your osu! songs folder");
        }

        etterna_songs = ChartFolders::etterna_songs_folder();

        if (etterna_songs) {
            link_etterna_button = &form.add<ButtonField>("Link your Etterna songs folder");
        }

        linked_folders_field = &form.add<ChoiceField>("Linked folders", std::vector<std::string>{"None"}, 0);
        refresh_linked_folders();

        data_folder_button = &form.add<ButtonField>("Open data folder");

        form.add<SectionField>("About");
        changelog_button = &form.add<ButtonField>(std::format("Changelog (version {})", NASCENT_VERSION_STRING));
        report_bug_button = &form.add<ButtonField>("Report a bug");
        discord_button = &form.add<ButtonField>("Join the Discord");
        credits_button = &form.add<ButtonField>("Credits");

        form.spacing = Ui::layout("options.field_spacing");
        form.focus_field(0);

        build_preview_chart();
        rebuild_preview();
    }

    void SceneOptions::apply() {
        Settings& settings = SettingsManager::settings;
        if (!skins.empty() && skins[skin_field->selected].id != skin->get_id()) {
            settings.skin = skins[skin_field->selected].id;
            skin->reload(settings.skin);
            report_skin_problems();
        }

        settings.master_volume = (uint8_t)master_volume_field->value;
        settings.music_volume = (uint8_t)music_volume_field->value;
        settings.effects_volume = (uint8_t)effects_volume_field->value;
        game->audio().set_volumes(SettingsManager::volume_gain(settings.master_volume), SettingsManager::volume_gain(settings.music_volume), SettingsManager::volume_gain(settings.effects_volume));
        std::string device_status;

        if (const std::string& device = output_devices[output_device_field->selected]; device != settings.audio_output_device) {
            settings.audio_output_device = device;

            if (!game->audio().engine().SetPlaybackDevice(device)) {
                device_status = std::format("Couldn't open {}, so sound is playing on the system default", device);
            }
        }

        settings.frame_rate_limit = frame_rates[frame_rate_field->selected];
        settings.discord_rich_presence = rich_presence_field->selected == 1;
        RichPresence::set_enabled(settings.discord_rich_presence);
        float transition_time = TRANSITION_OPTIONS[transition_field->selected].second;

        if (transition_time != settings.scene_transition_time) {
            settings.scene_transition_time = transition_time;
            game->set_transition(std::make_unique<FadeTransition>(transition_time));
        }
        WindowMode previous_mode = SettingsManager::window_mode();
        std::string previous_monitor = settings.display_monitor;
        settings.display_fullscreen = fullscreen_field->selected != 0;
        settings.display_borderless = fullscreen_field->selected == 2;

        if (monitor_field != nullptr && monitor_field->selected != monitor_shown) {
            monitor_shown = monitor_field->selected;
            settings.display_monitor = monitors[monitor_shown];
        }

        if ((SettingsManager::window_mode() != previous_mode || settings.display_monitor != previous_monitor) && window_placement_supported() && !SettingsManager::apply_window_mode()) {
            device_status = "Couldn't change the display mode now, it will apply when you restart the game";
        }
        settings.field_hitsounds = hitsounds_field->selected == 1;
        settings.field_mirror = mirror_field->selected == 1;
        settings.field_instant_restart = instant_restart_field->selected == 1;
        settings.field_lane_cover = (int8_t)lane_cover_field->value;
        settings.field_note_gradient = note_gradient_field->selected == 1;
        settings.title_songs = (TitleSongs)title_music_field->selected;
        settings.field_background_brightness = (uint8_t)background_brightness_field->value;
        settings.field_note_opacity = (uint8_t)note_opacity_field->value;
        settings.field_receptor_opacity = (uint8_t)receptor_opacity_field->value;
        settings.field_lane_gap = lane_gaps[lane_gap_field->selected];
        settings.field_audio_visual_offset = (int16_t)visual_offset_field->value;
        settings.field_audio_input_offset = (int16_t)input_offset_field->value;
        settings.field_scroll_speed = (uint16_t)scroll_speed_field->value;
        settings.field_upscroll = scroll_direction_field->selected == 1;

        for (SkinFollowing* field : skin_fields) {
            field->refresh_skin(*skin);
            field->store(settings);
        }

        rebuild_preview();
        status = device_status;

        for (KeyBindField* field : key_fields) {
            if (std::optional<KeyBinding> swapped = std::exchange(field->swapped, std::nullopt)) {
                status = std::format("{} was already on {}, it moved to {}", KEY_BINDINGS[(size_t)*swapped].label, InputManager::key_label(InputManager::get_key(field->binding)), InputManager::key_label(InputManager::get_key(*swapped)));
            }
        }

        changed = true;
    }

    void SceneOptions::on_enter() {
        form.set_enabled(true);

        if (visual_offset_field == nullptr) {
            return;
        }

        const Settings& settings = SettingsManager::settings;
        visual_offset_field->value = settings.field_audio_visual_offset;
        input_offset_field->value = settings.field_audio_input_offset;
        scroll_speed_field->value = settings.field_scroll_speed;
        rebuild_preview();
    }

    void SceneOptions::on_exit() {
        form.set_enabled(false);
        commit_server();

        if (std::exchange(changed, false)) {
            SettingsManager::save();
        }
    }

    bool SceneOptions::update_footer(Input& input) {
        std::optional<int32_t> clicked = footer_spots.take_click(input);

        if (!clicked) {
            return false;
        }

        if (*clicked == FOOTER_BACK) {
            input.inject(actions::BACK);
        }
        else if (*clicked == FOOTER_CONFIRM) {
            input.inject(actions::CONFIRM);
        }
        else if (*clicked == FOOTER_RESET && form.focused_field() == linked_folders_field) {
            unlink_requested = true;
        }
        else if (SkinFollowing* skin_field = dynamic_cast<SkinFollowing*>(form.focused_field()); *clicked == FOOTER_RESET && skin_field != nullptr && skin_field->use_skin_value()) {
            return true;
        }
        else if (KeyBindField* key_field = dynamic_cast<KeyBindField*>(form.focused_field()); key_field != nullptr && key_field->reset_binding()) {
            return true;
        }

        return false;
    }

    bool SceneOptions::searching_blocked() const {
        const UiField* focused = form.focused_field();
        const KeyBindField* key_field = dynamic_cast<const KeyBindField*>(focused);

        if (key_field != nullptr && key_field->is_capturing()) {
            return true;
        }

        if (dynamic_cast<const TextField*>(focused) == nullptr) {
            return false;
        }

        for (size_t index = 0; index < form.size(); index++) {
            if (form.field(index) == focused) {
                return !form.is_hidden(index);
            }
        }

        return false;
    }

    void SceneOptions::update_search(Input& input) {
        std::string before = search;

        if (search_spots.take_click(input) == SEARCH_CLEAR) {
            search.clear();
        }

        if (!searching_blocked()) {
            search += input.text();

            for (olc::Key key : input.pressed_keys()) {
                if (key == olc::Key::BACK && !search.empty()) {
                    search.pop_back();

                    while (!search.empty() && ((unsigned char)search.back() & 0xc0) == 0x80) {
                        search.pop_back();
                    }
                }
            }

            if (!search.empty() && input.take(actions::BACK)) {
                search.clear();
            }
        }

        if (search != before) {
            apply_search();
        }
    }

    void SceneOptions::apply_search() {
        std::vector<std::string> words = parse_search(search).words;
        std::vector<bool> hidden(form.size(), false);
        std::optional<size_t> section;
        std::string section_title;
        search_matches = 0;
        search_total = 0;

        for (size_t index = 0; index < form.size(); index++) {
            const UiField* field = form.field(index);

            if (const SectionField* heading = dynamic_cast<const SectionField*>(field)) {
                section = index;
                section_title = ascii_lowercase(heading->title);
                hidden[index] = !words.empty();
                continue;
            }

            std::string label;

            if (const RowField* row = dynamic_cast<const RowField*>(field)) {
                label = row->label;
            }
            else if (const TextField* text = dynamic_cast<const TextField*>(field)) {
                label = text->label;
            }

            std::string haystack = section_title + " " + ascii_lowercase(label);
            bool match = std::all_of(words.begin(), words.end(), [&](const std::string& word) {
                return haystack.find(word) != std::string::npos;
            });

            search_total++;
            hidden[index] = !match;

            if (match) {
                search_matches++;

                if (section) {
                    hidden[*section] = false;
                }
            }
        }

        form.set_hidden(std::move(hidden));
        scroll = scroll_target = 0;
    }

    void SceneOptions::draw_search(olc::PixelGameEngine* window, olc::vi2d position) {
        olc::Font& font = skin->display_font_small;
        float hint_gap = Ui::layout_pxf("options.footer_hint_gap");
        search_spots.clear();

        if (search.empty()) {
            std::string subtitle = "Changes apply right away and are saved when you leave.";
            draw_text(font, subtitle, position, skin->color(SkinColor::TEXT_MUTED));
            draw_text(font, "Type to search", {position.x + text_bounds(font, subtitle).size.x + (int32_t)hint_gap, position.y}, skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        draw_text(font, search, position, skin->color(SkinColor::TEXT));
        float x = (float)position.x + text_bounds(font, search).size.x + hint_gap;
        std::vector<Hint> hints = {{"Esc  clear", SEARCH_CLEAR}};
        draw_hints(window, font, hints, {x, (float)position.y}, hint_gap, skin->color(SkinColor::TEXT_FAINT), skin->color(SkinColor::TEXT), search_spots);
        x += hints_width(font, hints, hint_gap) + hint_gap;
        std::string count = search_matches == 0 ? std::string("No settings match") : std::format("{} / {}", search_matches, search_total);
        draw_text(font, count, {(int32_t)x, position.y}, skin->color(search_matches == 0 ? SkinColor::TEXT_ERROR : SkinColor::TEXT_MUTED));
    }

    void SceneOptions::report_skin_problems() {
        const std::vector<std::string>& problems = skin->problems();

        if (problems.empty()) {
            return;
        }

        std::string more = problems.size() > 1 ? std::format(" (and {} more, listed in nascent.log)", problems.size() - 1) : std::string();
        game->notify(std::format("{} has a problem: {}{}", skin->manifest().name, problems.front(), more), true);
    }

    void SceneOptions::export_selected_skin() {
        if (skins.empty() || skin_export.valid()) {
            return;
        }

        const SkinInfo& chosen = skins[skin_field->selected];
        std::filesystem::path folder = Skin::skin_directory(chosen.id);
        std::filesystem::path archive = user_config_directory(SettingsManager::APP_NAME) / SKIN_EXPORT_FOLDER / utf8_path(chosen.id + ".zip");
        exported_skin = chosen.name;
        skin_export = run_detached([folder, archive] {
            std::string error;
            return export_skin_archive(folder, archive, error) ? std::string() : error;
        });
    }

    void SceneOptions::delete_selected_skin() {
        if (skins.empty()) {
            return;
        }

        SkinInfo chosen = skins[skin_field->selected];
        std::error_code ec;

        if (std::filesystem::is_directory(Skin::builtin_skins_directory() / utf8_path(chosen.id), ec)) {
            game->notify(std::format("{} is built in, so it can't be deleted", chosen.name), true);
            return;
        }

        if (!delete_confirming) {
            delete_confirming = true;
            delete_skin_button->label = std::format("Press Enter again to delete {}", chosen.name);
            return;
        }

        delete_confirming = false;
        delete_skin_button->label = DELETE_SKIN_LABEL;

        if (chosen.id == skin->get_id()) {
            auto fallback = std::find_if(skins.begin(), skins.end(), [](const SkinInfo& info) {
                return info.id == Skin::DEFAULT_SKIN;
            });
            skin_field->selected = fallback != skins.end() ? (size_t)(fallback - skins.begin()) : 0;
            apply();
        }

        std::filesystem::remove_all(Skin::user_skins_directory() / utf8_path(chosen.id), ec);
        Skin::list_changed();
        game->notify(ec ? std::format("Couldn't delete {}: {}", chosen.name, ec.message()) : std::format("Deleted {}", chosen.name), (bool)ec);
    }

    void SceneOptions::update_skin_actions() {
        if (delete_confirming && form.focused_field() != delete_skin_button) {
            delete_confirming = false;
            delete_skin_button->label = DELETE_SKIN_LABEL;
        }

        if (!is_ready(skin_export)) {
            return;
        }

        std::string error = skin_export.get();

        if (!error.empty()) {
            game->notify(std::format("Couldn't export {}: {}", exported_skin, error), true);
            return;
        }

        game->notify(std::format("Exported {} to the exports folder", exported_skin));
        open_folder(user_config_directory(SettingsManager::APP_NAME) / SKIN_EXPORT_FOLDER);
    }

    SkinSliderField& SceneOptions::add_skin_slider(std::string label, SkinSetting setting, int32_t minimum, int32_t maximum) {
        SkinSliderField& field = form.add<SkinSliderField>(std::move(label), setting, *skin, minimum, maximum, PERCENT_STEP, "%");
        skin_fields.push_back(&field);
        position_fields.push_back(&field);
        return field;
    }

    SkinChoiceField& SceneOptions::add_skin_toggle(std::string label, SkinToggle toggle, std::string off_label, std::string on_label) {
        SkinChoiceField& field = form.add<SkinChoiceField>(std::move(label), toggle, *skin, std::move(off_label), std::move(on_label));
        skin_fields.push_back(&field);
        visibility_fields.push_back(&field);
        return field;
    }

    bool SceneOptions::reset_to_skin(const std::vector<SkinFollowing*>& fields) {
        bool changed = false;

        for (SkinFollowing* field : fields) {
            changed = field->use_skin_value() || changed;
        }

        return changed;
    }

    void SceneOptions::add_hud_position(HudElement element) {
        const HudSlot& slot = HUD_SLOTS[(size_t)element];
        add_skin_slider(std::format("{} X position", slot.label), slot.x, 0, 100);
        add_skin_slider(std::format("{} Y position", slot.label), slot.y, 0, 100);
    }

    void SceneOptions::report_bug() {
        std::string body = std::format("**What happened?**\n\n\n**What did you expect to happen?**\n\n\n**Steps to reproduce**\n\n\nVersion: {}\n\nPlease attach {} from the folder Nascent opened.", NASCENT_VERSION_STRING, SettingsManager::LOG_FILE);
        bool opened = open_url(std::format("{}?body={}", BUG_REPORT_URL, ServerClient::url_encode(body)));
        open_folder(user_config_directory(SettingsManager::APP_NAME));
        status = opened ? std::format("Describe the bug on GitHub and attach {} from the folder that opened", SettingsManager::LOG_FILE) : std::format("Couldn't open a browser. Report bugs at {}", BUG_REPORT_URL);
    }

    void SceneOptions::refresh_linked_folders() {
        const std::vector<std::string>& linked = SettingsManager::settings.list_linked_directories;
        linked_folders_field->options.clear();

        for (const std::string& folder : linked) {
            linked_folders_field->options.push_back(folder);
        }

        if (linked.empty()) {
            linked_folders_field->options.push_back("None");
        }

        linked_folders_field->selected = std::min(linked_folders_field->selected, linked_folders_field->options.size() - 1);
    }

    void SceneOptions::link_folder(const std::filesystem::path& folder) {
        FolderLinkResult result = ChartFolders::link(folder);
        game->notify(result.message, !result.ok);

        if (result.ok) {
            refresh_linked_folders();
            linked_folders_field->selected = linked_folders_field->options.size() - 1;
        }
    }

    void SceneOptions::update_linked_folders() {
        if (is_ready(folder_pick)) {
            std::optional<std::filesystem::path> chosen = folder_pick.get();

            if (!chosen) {
                game->notify("Couldn't open a folder picker. On Linux, install zenity or kdialog", true);
            }
            else if (!chosen->empty()) {
                link_folder(*chosen);
            }
        }

        bool unlink = std::exchange(unlink_requested, false) || (form.focused_field() == linked_folders_field && game->input().key_pressed(olc::Key::DEL));

        if (unlink && form.focused_field() == linked_folders_field && !SettingsManager::settings.list_linked_directories.empty()) {
            FolderLinkResult result = ChartFolders::unlink(linked_folders_field->selected);
            game->notify(result.message, !result.ok);
            refresh_linked_folders();
        }
    }

    void SceneOptions::refresh_skins() {
        skin_list_version = Skin::list_version();
        std::string chosen = skins.empty() ? skin->get_id() : skins[skin_field->selected].id;
        skins = Skin::available_skins();
        skin_field->options.clear();
        skin_field->selected = 0;

        for (size_t index = 0; index < skins.size(); index++) {
            skin_field->options.push_back(skins[index].name);

            if (skins[index].id == chosen) {
                skin_field->selected = index;
            }
        }
    }

    void SceneOptions::update(float elapsed_time) {
        if (master_volume_field != nullptr) {
            master_volume_field->value = SettingsManager::settings.master_volume;
        }

        if (skin_field != nullptr && Skin::list_version() != skin_list_version) {
            refresh_skins();
        }

        bool reset = update_footer(game->input());
        update_search(game->input());

        if (std::optional<float> position = scrollbar.update(game->input(), scroll_position())) {
            scroll_to(*position);
        }

        UiForm::Action action = form.update(game->input(), elapsed_time);

        if (reset) {
            action = UiForm::Action::CHANGED;
        }

        if (data_folder_button->take_pressed()) {
            open_folder(user_config_directory(SettingsManager::APP_NAME));
        }
        else if (link_folder_button->take_pressed()) {
            if (!folder_pick.valid()) {
                folder_pick = run_detached([] {
                    return choose_folder("Choose a folder of charts, like your osu! or Etterna Songs folder");
                });
            }
        }
        else if (link_osu_button != nullptr && link_osu_button->take_pressed()) {
            link_folder(*osu_songs);
        }
        else if (link_etterna_button != nullptr && link_etterna_button->take_pressed()) {
            link_folder(*etterna_songs);
        }
        else if (calibration_button->take_pressed()) {
            game->scenes().push(std::make_unique<SceneCalibration>(skin));
        }
        else if (export_skin_button->take_pressed()) {
            export_selected_skin();
        }
        else if (delete_skin_button->take_pressed()) {
            delete_selected_skin();
        }
        else if (credits_button->take_pressed()) {
            game->scenes().push(std::make_unique<SceneCredits>(skin));
        }
        else if (changelog_button->take_pressed()) {
            overlays.open(std::make_unique<ChangelogOverlay>(skin, Changelog::load(), "Changelog", "Every update so far"));
        }
        else if (reset_visibility_button->take_pressed()) {
            if (reset_to_skin(visibility_fields)) {
                apply();
            }
        }
        else if (reset_positions_button->take_pressed()) {
            if (reset_to_skin(position_fields)) {
                apply();
            }
        }
        else if (report_bug_button->take_pressed()) {
            report_bug();
        }
        else if (discord_button->take_pressed()) {
            status = open_url(DISCORD_URL) ? std::string("Opened the Nascent Discord in your browser") : std::format("Couldn't open a browser. Join the Discord at {}", DISCORD_URL);
        }
        else if (action == UiForm::Action::CHANGED) {
            apply();
        }

        if (action == UiForm::Action::MOVED || action == UiForm::Action::CHANGED) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        update_linked_folders();
        update_skin_actions();
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

        if (is_ready(server_check)) {
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
        double width = screen.y * skin_setting(*skin, skin_settings::LANE_WIDTH) / 100.0 * PREVIEW_KEY_COUNT;

        preview_field = std::make_unique<EntityField>(preview_chart.get(), skin, olc::vd2d{(double)screen.x * Ui::layout("options.preview_center") - width / 2, 0.0}, olc::vd2d{width, (double)screen.y});
        preview_field->draw_judge_colors = true;
        preview_field->draw_notes_past_judge = false;
        preview_field->draw_hit_score = skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT);
        preview_field->draw_combo = skin_toggle(*skin, skin_toggles::SHOW_COMBO);
        preview_field->judgement_height = skin_setting(*skin, skin_settings::JUDGEMENT_HEIGHT) / 100.0;
        preview_field->combo_height = skin_setting(*skin, skin_settings::COMBO_HEIGHT) / 100.0;
        preview_field->set_attempt(preview_attempt.get());
        preview_field->judge_alpha = (uint8_t)Ui::layout("options.preview_receptor_alpha");
        preview_field->judge_height = screen.y * skin_setting(*skin, skin_settings::JUDGEMENT_LINE_HEIGHT) / 100.0;
        preview_field->upscroll = settings.field_upscroll;
        preview_field->note_opacity = SettingsManager::settings.field_note_opacity / 100.0f;
        preview_field->receptor_opacity = SettingsManager::settings.field_receptor_opacity / 100.0f;
        preview_field->mirror = settings.field_mirror;
        preview_field->lane_cover = settings.field_lane_cover / 100.0;
        preview_field->note_gradient = settings.field_note_gradient;
        preview_field->draw_judge = skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT_LINE);
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
        int32_t margin = Ui::layout_px("options.scroll_margin");
        float top = (float)(form.focused_top() - margin);
        float bottom = (float)(form.focused_bottom() + margin - view_height);

        scroll_target = std::clamp(std::clamp(scroll_target, bottom, std::max(top, bottom)), 0.0f, max_scroll);
        scroll += (scroll_target - scroll) * std::min(1.0f, elapsed_time * (float)Ui::layout("options.scroll_ease_speed"));
    }

    float SceneOptions::scroll_position() const {
        float max_scroll = (float)std::max(0, form.height() - (viewport_bottom - viewport_top));
        return max_scroll > 0.0f ? scroll / max_scroll : 0.0f;
    }

    void SceneOptions::scroll_to(float position) {
        int32_t view_height = viewport_bottom - viewport_top;
        float max_scroll = (float)std::max(0, form.height() - view_height);
        float margin = Ui::layout_pxf("options.scroll_margin");
        scroll = scroll_target = position * max_scroll;

        if ((float)form.focused_top() - margin < scroll || (float)(form.focused_bottom() - view_height) + margin > scroll) {
            form.focus_nearest((int32_t)std::lround(scroll + view_height / 2.0f));
        }
    }

    std::vector<Hint> SceneOptions::footer_hints() const {
        const KeyBindField* key_field = dynamic_cast<const KeyBindField*>(form.focused_field());

        if (key_field != nullptr && key_field->is_capturing()) {
            return {{"Press the new key"}};
        }

        if (key_field != nullptr) {
            return {{"↑/↓:  choose"}, {"Enter:  rebind", FOOTER_CONFIRM}, {"Delete:  reset", FOOTER_RESET}, {"Esc:  back", FOOTER_BACK}};
        }

        if (form.focused_field() != nullptr && (form.focused_field() == reset_visibility_button || form.focused_field() == reset_positions_button)) {
            return {{"↑/↓:  choose"}, {"Enter:  reset", FOOTER_CONFIRM}, {"Esc:  back", FOOTER_BACK}};
        }

        if (const SkinFollowing* skin_field = dynamic_cast<const SkinFollowing*>(form.focused_field()); skin_field != nullptr) {
            if (skin_field->follows_skin) {
                return {{"↑/↓:  choose"}, {"←/→:  change"}, {"Esc:  back", FOOTER_BACK}};
            }

            return {{"↑/↓:  choose"}, {"←/→:  change"}, {"Delete:  use skin", FOOTER_RESET}, {"Esc:  back", FOOTER_BACK}};
        }

        if (form.focused_field() == linked_folders_field && !SettingsManager::settings.list_linked_directories.empty()) {
            return {{"←/→:  choose"}, {"Delete:  unlink", FOOTER_RESET}, {"Esc:  back", FOOTER_BACK}};
        }

        if (form.focused_field() == export_skin_button) {
            return {{"↑/↓:  choose"}, {"Enter:  export", FOOTER_CONFIRM}, {"Esc:  back", FOOTER_BACK}};
        }

        if (form.focused_field() == delete_skin_button) {
            return {{"↑/↓:  choose"}, {"Enter:  delete", FOOTER_CONFIRM}, {"Esc:  back", FOOTER_BACK}};
        }

        if (form.focused_field() == link_folder_button || (link_osu_button != nullptr && form.focused_field() == link_osu_button)) {
            return {{"↑/↓:  choose"}, {"Enter:  link", FOOTER_CONFIRM}, {"Esc:  back", FOOTER_BACK}};
        }

        if (form.focused_field() != nullptr && (form.focused_field() == data_folder_button || form.focused_field() == calibration_button || form.focused_field() == credits_button || form.focused_field() == changelog_button || form.focused_field() == report_bug_button || form.focused_field() == discord_button)) {
            return {{"↑/↓:  choose"}, {"Enter:  open", FOOTER_CONFIRM}, {"Esc:  back", FOOTER_BACK}};
        }

        if (server_field != nullptr && form.focused_field() == server_field) {
            return {{"Type an address"}, {"↑/↓:  choose"}, {"Esc:  save and go back", FOOTER_BACK}};
        }

        if (const SliderField* slider = dynamic_cast<const SliderField*>(form.focused_field()); slider != nullptr && slider->step > 1) {
            return {{"↑/↓:  choose"}, {"←/→:  change"}, {"Shift:  fine steps"}, {"Esc:  back", FOOTER_BACK}};
        }

        return {{"↑/↓:  choose"}, {"←/→:  change"}, {"Esc:  back", FOOTER_BACK}};
    }

    void SceneOptions::draw_scrollbar(olc::PixelGameEngine* window, int32_t x) {
        int32_t view_height = viewport_bottom - viewport_top;
        int32_t content_height = form.height();

        if (view_height <= 0 || content_height <= view_height) {
            scrollbar.hide();
            return;
        }

        float track_x = (float)(x + RowField::width() + Ui::layout_px("options.scrollbar_gap"));
        float track_width = std::max(1.0f, Ui::layout_pxf("options.scrollbar_width"));
        scrollbar.layout({{track_x, (float)viewport_top}, {track_width, (float)view_height}}, (float)view_height / (float)content_height);
        scrollbar.draw(window, scroll_position(), skin->color(SkinColor::SCROLLBAR_TRACK), skin->color(SkinColor::SCROLLBAR_THUMB));
    }

    void SceneOptions::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = (int32_t)(screen.x * Ui::layout("options.left"));
        int32_t y = (int32_t)(screen.y * Ui::layout("options.top"));

        olc::FontRect footer_metrics = text_bounds(skin->display_font_small, "Hgjy");
        int32_t footer_bottom = screen.y - (int32_t)(screen.y * Ui::layout("options.footer_bottom"));
        int32_t footer_top = footer_bottom - (footer_metrics.offset.y + footer_metrics.size.y);

        viewport_top = y + Ui::layout_px("options.header_height");
        viewport_bottom = footer_top - Ui::layout_px("options.footer_gap");
        float edge_fade = Ui::layout_pxf("options.edge_fade");
        form.set_clip(viewport_top, viewport_bottom);
        form.draw(window, *skin, {x, viewport_top - (int32_t)std::lround(scroll)});

        float max_scroll = (float)std::max(0, form.height() - (viewport_bottom - viewport_top));
        olc::Pixel mask = skin->color(SkinColor::OPTIONS_MASK);
        olc::Pixel clear = fade(mask, 0.0);
        window->FillRectDecal({0.0f, 0.0f}, {(float)screen.x, (float)viewport_top}, mask);
        window->FillRectDecal({0.0f, (float)viewport_bottom}, {(float)screen.x, (float)(screen.y - viewport_bottom)}, mask);

        if (scroll > 0.5f) {
            window->GradientFillRectDecal({0.0f, (float)viewport_top}, {(float)screen.x, edge_fade}, mask, clear, clear, mask);
        }

        if (scroll < max_scroll - 0.5f) {
            window->GradientFillRectDecal({0.0f, (float)viewport_bottom - edge_fade}, {(float)screen.x, edge_fade}, clear, mask, mask, clear);
        }

        draw_scrollbar(window, x);

        if (preview_field != nullptr) {
            preview_field->draw(window);

            if (skin_toggle(*skin, skin_toggles::SHOW_TIMING_BAR) && preview_attempt != nullptr) {
                draw_timing_bar(window, skin, *preview_attempt, (float)(screen.x * Ui::layout("options.preview_center")), (float)(screen.x * skin_setting(*skin, skin_settings::TIMING_BAR_LENGTH) / 100.0), (float)(screen.y * skin_setting(*skin, skin_settings::TIMING_BAR_WIDTH) / 100.0), (float)(skin_setting(*skin, skin_settings::TIMING_BAR_HEIGHT) / 100.0), skin_toggle(*skin, skin_toggles::SHOW_TIMING_AVERAGE));
            }
        }

        draw_text(skin->display_font_large, "Options", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::layout_px("options.subtitle_offset");
        draw_search(window, {x, y});

        if (!status.empty()) {
            draw_text(skin->display_font_small, status, {x, viewport_bottom + Ui::layout_px("options.status_offset")}, skin->color(SkinColor::TEXT_MUTED));
        }

        footer_spots.clear();
        draw_hints(window, skin->display_font_small, footer_hints(), {(float)x, (float)footer_top}, Ui::layout_pxf("options.footer_hint_gap"), skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), footer_spots);
    }
}
