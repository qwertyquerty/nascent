#include "SceneList.h"

#include "Account.h"
#include "BeatmapDownloads.h"
#include "ChartFolders.h"
#include "Chart.h"
#include "ChartSearch.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "EntityField.h"
#include "Favorites.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "LocalBests.h"
#include "LocalReplays.h"
#include "gse/Platform.h"
#include "gse/ui/ProgressBar.h"
#include "Results.h"
#include "SceneResults.h"
#include "Attempt.h"
#include "Replay.h"
#include "RichPresence.h"
#include "SceneAccount.h"
#include "SceneBrowser.h"
#include "SceneCalibration.h"
#include "SceneInput.h"
#include "SceneOptions.h"
#include "ScenePlay.h"
#include "SceneTitle.h"
#include "ServerApi.h"
#include "GameNascent.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "Utf8Path.h"
#include "gse/render/SpriteDecal.h"
#include "gse/util/Math.h"
#include "gse/render/Color.h"
#include "Util.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <initializer_list>
#include <iostream>
#include <numbers>
#include <string_view>
#include <tuple>
#include <utility>

namespace nascent {
    static constexpr uint8_t PREVIEW_FULL_WIDTH_KEY_COUNT = 4;

    struct RecommendedSong {
        int64_t set_id;
        const char* artist;
        const char* title;
    };

    template <typename F>
    static void chunked_for(size_t count, F work) {
        constexpr size_t CHUNK = 2048;

        if (count <= CHUNK) {
            for (size_t index = 0; index < count; index++) {
                work(index);
            }

            return;
        }

        parallel_for((count + CHUNK - 1) / CHUNK, [&](size_t chunk) {
            for (size_t index = chunk * CHUNK; index < std::min(count, (chunk + 1) * CHUNK); index++) {
                work(index);
            }
        });
    }

    static constexpr RecommendedSong RECOMMENDED_SONGS[] = {
        {1488740, "Orange Lounge", "100sec. Kitchen Battle!!"},
        {2170099, "fiend", "2DF2"},
        {2236810, "Minato Aqua", "Aqua Cat"},
        {772809, "Silentroom", "Finixe"},
        {2199227, "M2U", "Happy birthday to You (M2U Version)"},
        {2238570, "Bruno Coulais", "Les avions en papier"},
        {2146559, "JVKE", "this is what falling in love feels like"},
        {884961, "Akira Complex", "Ether Strike"},
        {2292680, "Satoru Kosaki feat. issei", "Into the world (TV Size)"},
        {2339429, "petit milady", "Koi wa Milk Tea (TV Size)"},
    };

    std::optional<std::vector<std::filesystem::path>> SceneList::chart_paths(bool favorites_only) {
        std::lock_guard<std::mutex> lock(library_load_mutex);
        const ChartLibrary* source = completed_library_load ? &*completed_library_load : (library_loaded ? &library : nullptr);

        if (source == nullptr) {
            return std::nullopt;
        }

        std::vector<std::filesystem::path> paths;

        for (size_t index = 0; index < source->charts.size() && index < source->hashes.size(); index++) {
            if (!favorites_only || Favorites::contains(source->hashes[index])) {
                paths.push_back(source->charts[index]->file_path);
            }
        }

        return paths;
    }

    bool SceneList::library_available() {
        std::lock_guard<std::mutex> lock(library_load_mutex);
        return library_loaded || completed_library_load.has_value();
    }

    float SceneList::library_progress() {
        return library_available() ? 1.0f : library_load_progress.fraction();
    }

    SceneList::SceneList(Skin* skin, Skin* logo_skin, EntityStarfield* sky) {
        this->skin = skin;
        this->logo_skin = logo_skin;
        this->sky = sky;
        difficulty_cache = std::make_unique<DifficultyCache>(user_config_directory(SettingsManager::APP_NAME) / DIFFICULTY_CACHE_FILE);
        library_cache = std::make_unique<LibraryCache>(user_config_directory(SettingsManager::APP_NAME));

        background_loader = std::thread([this] {
            background_load_loop();
        });

        difficulty_worker = std::thread([this] {
            difficulty_loop();
        });

        for (unsigned worker = 0; worker < std::max(1u, std::thread::hardware_concurrency() / 2); worker++) {
            rate_workers.emplace_back([this] {
                rate_loop();
            });
        }

        start_library_load();
    };

    SceneList::~SceneList() {
        {
            std::lock_guard<std::mutex> lock(background_load_mutex);
            background_load_stopping = true;
        }

        background_load_cv.notify_all();

        if (background_loader.joinable()) {
            background_loader.join();
        }

        if (library_loader.joinable()) {
            library_loader.join();
        }

        {
            std::lock_guard<std::mutex> lock(difficulty_mutex);
            difficulty_stopping = true;
        }

        difficulty_cv.notify_all();

        if (difficulty_worker.joinable()) {
            difficulty_worker.join();
        }

        for (std::thread& worker : rate_workers) {
            worker.join();
        }

        if (difficulty_cache_save.valid()) {
            difficulty_cache_save.wait();
        }

        std::string error;

        if (!difficulty_cache->save(error)) {
            std::cerr << "Difficulty cache: " << error << std::endl;
        }
    }

    void SceneList::on_create() {
        leaderboards.set_mode(SettingsManager::settings.list_leaderboard_mode == (uint8_t)LeaderboardMode::PERFORMANCE ? LeaderboardMode::PERFORMANCE : LeaderboardMode::ACCURACY);

        if (!pending_selection_path.empty()) {
            std::string error;
            std::unique_ptr<Chart> chart = load_chart_file(pending_selection_path, pitch, error, pending_selection.value_or(std::string()));

            if (chart != nullptr) {
                apply_library(single_chart_library(std::move(chart), pitch, *difficulty_cache));
            }
        }

        if (!library_loading) {
            start_library_load();
        }
    };

    void SceneList::on_enter() {
        active = true;
        pause_rates(false);
        search_data_version++;

        if (current_sort() == ListSort::RECENTLY_PLAYED || current_sort() == ListSort::BEST_ACCURACY) {
            sort_order_dirty = true;
        }
        RichPresence::picking_song(selected_chart);

        bool downloads_changed = BeatmapDownloads::take_library_changed();

        if (ChartFolders::take_changed() || downloads_changed) {
            start_library_load();
        }

        refresh_search();

        if (pending_selection) {
            auto found = std::find(library.hashes.begin(), library.hashes.end(), *pending_selection);
            int audio_id = std::exchange(pending_audio_id, -1);
            pending_selection.reset();
            pending_selection_path.clear();

            if (found != library.hashes.end()) {
                size_t chart = (size_t)(found - library.hashes.begin());
                apply_search(chart);

                if (std::find(visible.begin(), visible.end(), chart) == visible.end()) {
                    search.clear();
                    apply_search(chart);
                }

                if (audio_id != -1) {
                    stop_preview_audio();
                    selected_chart_audio_id = audio_id;
                    preview_audio_chart = library.charts[chart].get();
                    game->audio().cancel_fade(selected_chart_audio_id);
                    audio_id = -1;
                }
            }

            if (audio_id != -1) {
                game->audio().engine().Stop(audio_id);
                game->audio().engine().UnloadSound(audio_id);
            }
        }

        if (!visible.empty()) {
            select_chart(selected_chart_index, false);
        }

        timer = 0;
        status_bar.refresh();
        leaderboards.clear();
    }

    void SceneList::on_exit() {
        active = false;
        pause_rates(true);
        stop_preview_audio();
        load_generation++;
        replay_download_hash.clear();
    }

    void SceneList::on_resize() {
        widest_grade = -1.0f;

        if (selected_chart != nullptr) {
            create_preview_field();
        }
    }

    void SceneList::update(float elapsed_time) {
        status_bar.update(elapsed_time);
        poll_library_load();
        collect_difficulties();
        retire_idle_charts();
        save_difficulty_cache_later();

        if (difficulty_refresh_pending && now_ms() - difficulty_refreshed_at >= DIFFICULTY_REFRESH_MS) {
            difficulty_refresh_pending = false;
            difficulty_refreshed_at = now_ms();
            sort_order_dirty = sort_order_dirty || current_sort() == ListSort::DIFFICULTY;
            float kept_offset = list_scroll_offset;
            refresh_search();
            list_scroll_offset = kept_offset;

            if (!visible.empty() && library.charts[visible[selected_chart_index]].get() != selected_chart) {
                select_chart(selected_chart_index, false);
            }
        }
        update_background_work(elapsed_time);
        leaderboards.update(elapsed_time, show_leaderboards && selected_chart != nullptr && !visible.empty(), selected_hash(), pitch);
        poll_replay_download();

        if (!update_controls(elapsed_time)) {
            return;
        }

        if (visible.empty()) {
            return;
        }

        std::optional<BackgroundLoadResult> load_result;
        {
            std::lock_guard<std::mutex> lock(background_load_mutex);
            load_result = std::exchange(completed_background_load, std::nullopt);
        }

        if (load_result && load_result->generation == load_generation) {
            apply_background_load(std::move(*load_result));
        }

        crossfade_timer = std::max(0.0f, (crossfade_timer - elapsed_time * (float)Ui::layout("song_list.background_crossfade_speed")));

        double now = now_ms();

        if (selected_chart_audio_id != -1) {
            game->audio().apply_music_volume(selected_chart_audio_id, game->audio().music_fade(selected_chart_audio_id));
        }

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart && !game->audio().engine().IsPlaying(selected_chart_audio_id)) {
            game->audio().engine().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
            game->audio().engine().Play(selected_chart_audio_id, true);
        }

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart) {
            preview_clock.sync(game->audio().engine().GetPositionMilliseconds(selected_chart_audio_id) / selected_chart->pitch, now);
        }
        else {
            preview_clock.reset(selected_chart->info.preview_time / selected_chart->pitch, now);
        }

        preview_position = preview_clock.position_at(now);
        field->set_position(preview_position);
        field->opacity = selected_chart_audio_id != -1 && preview_audio_chart == selected_chart ? game->audio().fade_progress(selected_chart_audio_id) : 0.0f;

        timer += elapsed_time;

        if (crossfade && crossfade_timer == 0.0f) {
            crossfade = false;
            old_background_image.reset();
        }

        list_scroll_offset *= std::abs(list_scroll_offset) > 1.0 ? (std::pow(0.005, elapsed_time * Ui::layout("song_list.scroll_settle_speed"))) : 0;
    };

    bool SceneList::run_status_action(StatusAction action, bool secondary) {
        if (secondary && action != StatusAction::RATE) {
            return false;
        }

        switch (action) {
            case StatusAction::TOGGLE_SCORES:
                show_leaderboards = !show_leaderboards;
                status_bar.set_leaderboards_shown(show_leaderboards);
                leaderboards.show_now();
                game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
                return false;
            case StatusAction::RATE:
                set_pitch(pitch + (secondary ? -PITCH_INTERVAL : PITCH_INTERVAL));
                return false;
            case StatusAction::CHART_MENU:
                if (visible.empty()) {
                    return false;
                }

                open_chart_menu();
                return true;
            case StatusAction::SORT:
                open_sort_menu();
                return true;
            default:
                break;
        }

        game->audio().play_effect(skin->get_random_menu_pluck());

        switch (action) {
            case StatusAction::CALIBRATION:
                game->scenes().push(std::make_unique<SceneCalibration>(skin));
                break;
            case StatusAction::ACCOUNT:
                game->scenes().push(std::make_unique<SceneAccount>(skin, nullptr, sky));
                break;
            case StatusAction::BROWSE:
                game->scenes().push(std::make_unique<SceneBrowser>(skin, this));
                break;
            case StatusAction::OPTIONS:
                game->scenes().push(std::make_unique<SceneOptions>(skin));
                break;
            default:
                game->scenes().push(std::make_unique<SceneInput>(skin, selected_chart != nullptr ? selected_chart->info.key_count : (uint8_t)4));
                break;
        }

        return true;
    }

    void SceneList::play_selected() {
        auto play = std::make_unique<ScenePlay>(selected_chart, skin);
        play->use_background(background_for(*selected_chart));

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart) {
            play->use_audio(std::exchange(selected_chart_audio_id, -1));
            preview_audio_chart = nullptr;
        }

        game->scenes().push(std::move(play));
    }

    bool SceneList::update_mouse(Input& input) {
        for (uint32_t button : {0u, 1u}) {
            if (std::optional<StatusAction> action = status_bar.take_click(input, button)) {
                if (run_status_action(*action, button == 1)) {
                    return true;
                }
            }
        }

        if (std::optional<int32_t> hovered = ui_spots.at(input.mouse_position()); hovered && *hovered >= SPOT_RECOMMENDED_FIRST && ui_spots.take_click(input, 1)) {
            size_t song = (size_t)(*hovered - SPOT_RECOMMENDED_FIRST);

            if (song < std::size(RECOMMENDED_SONGS) && BeatmapDownloads::cancel(RECOMMENDED_SONGS[song].set_id)) {
                game->audio().play_effect(skin->get_random_menu_pluck());
            }

            return false;
        }

        if (std::optional<int32_t> spot = ui_spots.take_click(input)) {
            if (*spot == SPOT_SEARCH_CLEAR) {
                search.clear();
                apply_search(selected_library_index());

                if (!visible.empty()) {
                    select_chart(selected_chart_index, false);
                }
            }
            else if (*spot == SPOT_DOWNLOAD) {
                download_recommended();
            }
            else if (*spot == SPOT_BROWSE) {
                return run_status_action(StatusAction::BROWSE, false);
            }

            return false;
        }

        if (visible.empty()) {
            return false;
        }

        if (update_preview_seek(input)) {
            return false;
        }

        if (show_leaderboards) {
            if (leaderboards.update_scrollbars(input)) {
                return false;
            }

            if (leaderboards.take_mode_click(input)) {
                toggle_leaderboard_mode();
                return false;
            }

            if (std::optional<LeaderboardPick> pick = leaderboards.take_click(input)) {
                open_leaderboard_play(std::move(*pick));
                return false;
            }
        }

        if (std::optional<float> position = list_scrollbar.update(input, list_position())) {
            int32_t index = (int32_t)std::lround(*position * (float)(visible.size() - 1));

            if (index != selected_chart_index) {
                select_chart(index, false);
            }

            return false;
        }

        for (uint32_t button : {0u, 1u}) {
            std::optional<int32_t> row = row_spots.take_click(input, button);

            if (!row) {
                continue;
            }

            int32_t offset = *row - ROW_SPOT_CENTER;

            if (offset != 0) {
                select_chart(positive_modulo(selected_chart_index + offset, visible.size()));
            }

            if (button == 1) {
                open_chart_menu();
                return true;
            }

            if (offset == 0) {
                play_selected();
                return true;
            }
        }

        return false;
    }

    bool SceneList::update_preview_seek(Input& input) {
        if (selected_chart == nullptr || selected_chart_audio_id == -1 || preview_audio_chart != selected_chart) {
            preview_seek.hide();
            preview_seek_target = -1;
            return false;
        }

        double end = std::max(1, selected_chart->get_chart_end_ms());
        std::optional<float> fraction = preview_seek.update(input, (float)std::clamp(preview_position / end, 0.0, 1.0));

        if (!fraction) {
            preview_seek_target = -1;
            return false;
        }

        double target = *fraction * end;

        if (preview_seek_target >= 0 && std::abs(target - preview_seek_target) < Ui::layout("song_list.preview_seek_step_ms")) {
            return true;
        }

        preview_seek_target = target;
        game->audio().engine().Seek(selected_chart_audio_id, (uint64_t)std::max(0.0, target * selected_chart->pitch));
        preview_clock.reset(target, now_ms());
        preview_position = target;
        field->set_position(preview_position);
        return true;
    }

    void SceneList::draw_preview_seek(olc::PixelGameEngine* window) {
        if (selected_chart == nullptr || selected_chart_audio_id == -1 || preview_audio_chart != selected_chart || field->opacity <= 0.0f) {
            preview_seek.hide();
            return;
        }

        olc::vi2d screen = window->GetScreenSize();
        olc::vi2d position = side_panel_position(screen);
        olc::vi2d size = side_panel_size(screen);
        float margin = Ui::layout_pxf("song_list.preview_seek_margin");
        float height = std::max(1.0f, Ui::layout_pxf("song_list.preview_seek_height"));
        float top = (float)(position.y + size.y) - Ui::layout_pxf("song_list.preview_seek_gap") - height;
        double end = std::max(1, selected_chart->get_chart_end_ms());
        preview_seek.layout({{(float)position.x + margin, top}, {std::max(1.0f, (float)size.x - margin * 2.0f), height}}, 0.0f, ScrollAxis::HORIZONTAL);
        preview_seek.draw(window, (float)std::clamp(preview_position / end, 0.0, 1.0), fade(skin->color(SkinColor::SCROLLBAR_TRACK), field->opacity), fade(skin->color(SkinColor::SCROLLBAR_THUMB), field->opacity));
    }

    bool SceneList::update_controls(float elapsed_time) {
        Input& input = game->input();

        if (update_mouse(input)) {
            return false;
        }

        if (input.key_pressed(InputManager::full_reload_key) && input.key_held(InputManager::full_reload_modifier)) {
            reload_everything();
            return false;
        }

        for (auto [key, action] : {
                 std::pair{InputManager::calibration_key, StatusAction::CALIBRATION},
                 std::pair{InputManager::account_key, StatusAction::ACCOUNT},
                 std::pair{InputManager::browse_key, StatusAction::BROWSE},
                 std::pair{InputManager::options_key, StatusAction::OPTIONS},
                 std::pair{InputManager::input_key, StatusAction::INPUT},
             }) {
            if (input.key_pressed(key)) {
                run_status_action(action, false);
                return false;
            }
        }

        if (search.empty() && input.take(actions::BACK)) {
            game->audio().play_effect(skin->get_random_menu_pluck());
            std::optional<TitleTrack> track;

            if (selected_chart_audio_id != -1 && preview_audio_chart != nullptr && game->audio().engine().IsPlaying(selected_chart_audio_id)) {
                track = TitleTrack{preview_audio_chart->file_path, std::exchange(selected_chart_audio_id, -1)};
                preview_audio_chart = nullptr;
            }

            game->scenes().replace(std::make_unique<SceneTitle>(skin, logo_skin, this, sky, std::move(track)));
            return false;
        }

        update_search();

        if (input.key_pressed(InputManager::leaderboard_key)) {
            if (input.key_held(InputManager::keybind_mod1)) {
                toggle_leaderboard_mode();
            }
            else {
                run_status_action(StatusAction::TOGGLE_SCORES, false);
            }
        }

        if (input.take(game_actions::SORT_MENU)) {
            open_sort_menu();
            return true;
        }

        if (input.key_held(InputManager::keybind_mod1)) {
            if (input.take(actions::LEFT)) {
                set_pitch(pitch - PITCH_INTERVAL);
            }
            if (input.take(actions::RIGHT)) {
                set_pitch(pitch + PITCH_INTERVAL);
            }
        }

        if (library.charts.empty() && (!library_loading || recommended_started) && input.take(actions::CONFIRM)) {
            download_recommended();
        }

        if (visible.empty()) {
            return false;
        }

        if (game->input().take(game_actions::CHART_MENU)) {
            open_chart_menu();
            return true;
        }

        if (input.key_pressed(InputManager::random_chart_key) && input.key_held(olc::Key::SHIFT)) {
            if (!shuffle_history.empty()) {
                auto [hash, path] = std::move(shuffle_history.back());
                shuffle_history.pop_back();
                select_chart_by_hash(hash, path);
            }
        }
        else if (input.key_pressed(InputManager::random_chart_key)) {
            if (selected_chart != nullptr) {
                shuffle_history.emplace_back(selected_chart->gameplay_hash(), selected_chart->file_path);

                if (shuffle_history.size() > SHUFFLE_HISTORY_LIMIT) {
                    shuffle_history.erase(shuffle_history.begin());
                }
            }

            select_chart(rand() % visible.size());
        }

        if (down_repeat.update(input.take(actions::DOWN), input.held(actions::DOWN), elapsed_time)) {
            select_chart(positive_modulo((selected_chart_index + 1), visible.size()));
        }

        if (up_repeat.update(input.take(actions::UP), input.held(actions::UP), elapsed_time)) {
            select_chart(positive_modulo((selected_chart_index - 1), visible.size()));
        }

        int mwheel = input.mouse_wheel();

        if (mwheel != 0 && show_leaderboards && leaderboards.scroll(input.mouse_position(), mwheel)) {
            mwheel = 0;
        }

        if (mwheel != 0) {
            select_chart(positive_modulo(
                selected_chart_index + ((mwheel < 0) - (mwheel > 0)),
                visible.size()
            ));
        }

        if (input.take(actions::CONFIRM)) {
            play_selected();
        }

        return true;
    }

    void SceneList::ensure_notes(Chart* chart) {
        if (chart == nullptr || chart->has_notes()) {
            return;
        }

        if (std::optional<std::vector<Hit>> notes = library_cache->load_notes(*chart)) {
            chart->restore_notes(std::move(*notes));
            return;
        }

        if (std::unique_ptr<Chart> loaded = load_chart_notes(nullptr, *chart)) {
            chart->restore_notes(loaded->unpitched_hit_objects());
        }
    }

    void SceneList::select_chart(int32_t index, bool sound) {
        Chart* previous = selected_chart;
        selected_chart = library.charts[visible[index]].get();
        ensure_notes(selected_chart);

        if (previous != nullptr && previous != selected_chart) {
            previous->release_notes();
        }
        status_bar.set_key_count(selected_chart->info.key_count);

        if (active) {
            RichPresence::picking_song(selected_chart);
        }

        leaderboards.wait_for_selection();
        selected_chart->set_pitch(this->pitch);

        int32_t down_steps = (index - selected_chart_index + visible.size()) % visible.size();
        int32_t up_steps = (selected_chart_index - index + visible.size()) % visible.size();
        int32_t row_height = Ui::layout_px("song_list.row_height");
        int32_t scroll_limit = row_height * (int32_t)Ui::layout("song_list.scroll_max_rows");

        list_scroll_offset += std::clamp(
            (up_steps < down_steps ? -up_steps : down_steps) * row_height,
            -scroll_limit,
            scroll_limit
        );

        selected_chart_index = index;

        if (sound) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        create_preview_field();

        double now = now_ms();
        preview_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;

        if (selected_chart_audio_id != -1 && preview_audio_chart != nullptr && preview_audio_chart->audio_path == selected_chart->audio_path) {
            preview_audio_chart = selected_chart;
            game->audio().engine().SetPitch(selected_chart_audio_id, selected_chart->pitch);
            preview_clock.reset(game->audio().engine().GetPositionMilliseconds(selected_chart_audio_id) / selected_chart->pitch, now);
        }
        else {
            preview_clock.reset(selected_chart->info.preview_time / selected_chart->pitch, now);
        }

        preview_position = preview_clock.position_at(now);
        field->set_position(preview_position);
        field->opacity = 0.0f;

        request_background_load();
    }

    void SceneList::create_preview_field() {
        olc::vi2d screen = game->window->GetScreenSize();
        olc::vi2d position = side_panel_position(screen);
        olc::vi2d size = side_panel_size(screen);
        float opacity = field ? field->opacity : 0.0f;
        double width = size.x * std::min(1.0, (double)selected_chart->info.key_count / PREVIEW_FULL_WIDTH_KEY_COUNT);
        double left = position.x + (size.x - width) / 2.0;

        field = std::make_unique<EntityField>(selected_chart, skin, olc::vd2d{left, 0.0}, olc::vd2d{width, (double)screen.y});
        field->draw_judge = false;
        field->upscroll = SettingsManager::settings.field_upscroll;
        field->note_opacity = SettingsManager::settings.field_note_opacity / 100.0f;
        field->receptor_opacity = SettingsManager::settings.field_receptor_opacity / 100.0f;
        field->mirror = SettingsManager::settings.field_mirror;
        field->note_gradient = SettingsManager::settings.field_note_gradient;
        field->set_position(preview_position);
        field->opacity = opacity;
    }

    olc::vi2d SceneList::side_panel_position(olc::vi2d screen) const {
        double width_fraction = Ui::layout("song_list.side_panel_width_fraction");
        int32_t width = side_panel_size(screen).x;
        int32_t margin = (int32_t)(screen.x * Ui::layout("song_list.margin_fraction"));
        return {(int32_t)(width * (1.0 - width_fraction) / width_fraction) - margin, StatusBar::height()};
    }

    olc::vi2d SceneList::side_panel_size(olc::vi2d screen) const {
        return {(int32_t)(screen.x * Ui::layout("song_list.side_panel_width_fraction")), screen.y - StatusBar::height() * 2};
    }

    std::string SceneList::selected_hash() const {
        return selected_chart != nullptr ? selected_chart->gameplay_hash() : std::string();
    }

    const MipDecal* SceneList::background_for(const Chart& chart) const {
        if (!background_image || chart.info.image_file.empty() || background_image_path != chart.image_path.string()) {
            return nullptr;
        }

        return &background_image;
    }

    void SceneList::request_background_load() {
        load_generation++;

        std::string image_path = selected_chart->info.image_file.empty() ? "" : selected_chart->image_path.string();
        std::filesystem::path audio_path = preview_audio_chart == selected_chart ? std::filesystem::path() : selected_chart->audio_path;
        bool keep_image = background_image && !image_path.empty() && image_path == background_image_path;

        {
            std::lock_guard<std::mutex> lock(background_load_mutex);
            pending_background_load = BackgroundLoadRequest{load_generation, image_path, audio_path, keep_image};
        }

        background_load_cv.notify_one();
    }

    void SceneList::background_load_loop() {
        while (true) {
            BackgroundLoadRequest request;

            {
                std::unique_lock<std::mutex> lock(background_load_mutex);

                background_load_cv.wait(lock, [this] { return background_load_stopping || pending_background_load.has_value(); });

                if (background_load_stopping) {
                    return;
                }

                request = std::move(*pending_background_load);
                pending_background_load.reset();
            }

            std::optional<PreparedSongAudio> audio;

            if (!request.audio_path.empty()) {
                audio = prepare_song_audio(request.audio_path);
            }

            std::unique_ptr<olc::Sprite> sprite;

            if (!request.keep_image && !request.image_path.empty()) {
                sprite = std::make_unique<olc::Sprite>();

                if (sprite->LoadFromFile(request.image_path) != olc::OK) {
                    sprite.reset();
                }
            }

            std::vector<std::unique_ptr<olc::Sprite>> levels = request.keep_image ? std::vector<std::unique_ptr<olc::Sprite>>() : MipDecal::build_levels(std::move(sprite));
            std::lock_guard<std::mutex> lock(background_load_mutex);
            completed_background_load = BackgroundLoadResult{request.generation, request.image_path, std::move(levels), request.keep_image, std::move(audio)};
        }
    }

    void SceneList::apply_background_load(BackgroundLoadResult result) {
        if (!result.keep_image) {
            old_background_image = std::move(background_image);
            background_image = MipDecal(std::move(result.levels), true, true);
            background_image_path = std::move(result.image_path);
            crossfade_timer = 1;
            crossfade = true;
        }

        if (preview_audio_chart == selected_chart) {
            return;
        }

        stop_preview_audio();

        bool prepared = result.audio && result.audio->path == selected_chart->audio_path;
        selected_chart_audio_id = prepared ? load_song_audio(game->audio().engine(), std::move(*result.audio)) : load_song_audio(game->audio().engine(), selected_chart->audio_path);
        preview_audio_chart = selected_chart;
        game->audio().fade_music(selected_chart_audio_id, 0.0f, 1.0f, SettingsManager::settings.list_preview_fade_in_time, false, PREVIEW_FADE_CURVE);
        game->audio().engine().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
        game->audio().engine().SetPitch(selected_chart_audio_id, selected_chart->pitch);
        game->audio().engine().Play(selected_chart_audio_id, true);
    }

    void SceneList::stop_preview_audio() {
        if (selected_chart_audio_id != -1) {
            game->audio().stop(selected_chart_audio_id);
            game->audio().engine().UnloadSound(selected_chart_audio_id);
            selected_chart_audio_id = -1;
            preview_audio_chart = nullptr;
        }
    }

    void SceneList::reload_everything() {
        difficulty_cache->clear();
        full_rebuild_pending = true;
        game->notify("Reloading every chart and recalculating difficulties");
        start_library_load();
    }

    void SceneList::start_library_load() {
        BeatmapDownloads::forget_missing();

        if (library_loading) {
            library_reload_pending = true;
            return;
        }

        library_loading = true;
        library_load_finished = false;
        library_load_progress.reset();
        std::vector<std::filesystem::path> song_directories = ChartFolders::all();
        float load_pitch = pitch;
        DifficultyCache* cache = difficulty_cache.get();
        LibraryCache* listing = library_cache.get();
        bool rebuild = std::exchange(full_rebuild_pending, false);

        if (rebuild) {
            listing->clear();
        }

        bool has_library = library_loaded && !rebuild;
        bool use_cache = !library_loaded && !rebuild && LocalReplays::folders_migrated();

        library_loader = std::thread([this, song_directories, load_pitch, cache, listing, has_library, use_cache] {
            bool base = has_library;

            if (!difficulty_cache_loaded) {
                cache->load();
                difficulty_cache_loaded = true;
            }

            if (use_cache) {
                ChartLibrary cached = cached_chart_library(*listing, song_directories, load_pitch, *cache);

                if (!cached.charts.empty()) {
                    std::lock_guard<std::mutex> lock(library_load_mutex);
                    completed_library_load = std::move(cached);
                    base = true;
                }
            }

            LibraryUpdate update = scan_chart_library(song_directories, load_pitch, *cache, *listing, &library_load_progress);

            if (!base) {
                prepare_library_update(update, load_pitch, *cache, *listing, &library_load_progress);
                ChartLibrary full = std::move(update.library);
                prepare_replay_folders(full, &library_load_progress);
                library_load_progress.begin(LibraryLoadStage::DONE, 0);
                std::lock_guard<std::mutex> lock(library_load_mutex);
                completed_library_load = std::move(full);
            }
            else if (update.changed) {
                prepare_library_update(update, load_pitch, *cache, *listing);
                std::lock_guard<std::mutex> lock(library_load_mutex);
                completed_library_update = std::move(update);
            }

            library_load_finished = true;
        });
    }

    void SceneList::poll_library_load() {
        bool downloads_changed = BeatmapDownloads::take_library_changed();

        if (ChartFolders::take_changed() || downloads_changed) {
            start_library_load();
        }

        if (!library_loading || game->scenes().is_transitioning()) {
            return;
        }

        bool finished = library_load_finished;
        std::optional<ChartLibrary> loaded;
        std::optional<LibraryUpdate> update;
        {
            std::lock_guard<std::mutex> lock(library_load_mutex);
            loaded = std::exchange(completed_library_load, std::nullopt);
            update = std::exchange(completed_library_update, std::nullopt);
        }

        if (loaded) {
            if (!LocalBests::is_backfilled()) {
                LocalBests::backfill(LocalReplays::gameplay_hash_by_folder(loaded->hashes), LocalReplays::directory());
            }

            apply_library(std::move(*loaded));
            library_loaded = true;
        }

        if (update) {
            std::vector<std::unique_ptr<Chart>> removed;
            ChartLibrary merged = merge_chart_library(std::move(library), std::move(*update), pitch, *difficulty_cache, removed);
            apply_library(std::move(merged), std::move(removed));
        }

        if (!finished) {
            return;
        }

        library_loader.join();
        library_loading = false;

        if (std::exchange(library_reload_pending, false)) {
            start_library_load();
        }
    }

    void SceneList::apply_library(ChartLibrary loaded, std::vector<std::unique_ptr<Chart>> removed) {
        std::string keep_selected = selected_hash();
        std::string keep_preview = preview_audio_chart != nullptr ? preview_audio_chart->gameplay_hash() : std::string();
        Chart* previous = selected_chart;

        cancel_difficulties(false);

        for (std::unique_ptr<Chart>& chart : library.charts) {
            if (chart != nullptr) {
                retired_charts.push_back(std::move(chart));
            }
        }

        for (std::unique_ptr<Chart>& chart : removed) {
            if (chart != nullptr) {
                retired_charts.push_back(std::move(chart));
            }
        }

        library = std::move(loaded);
        library_version++;
        search_data_version++;
        sort_order_dirty = true;
        local_hashes = std::unordered_set<std::string>(library.hashes.begin(), library.hashes.end());
        queue_all_rates();

        if (library.pitch != pitch) {
            library.difficulties.assign(library.size(), std::nullopt);
        }

        fill_difficulties();

        auto find_chart = [&](const std::string& hash) {
            auto found = std::find(library.hashes.begin(), library.hashes.end(), hash);
            return !hash.empty() && found != library.hashes.end() ? (size_t)(found - library.hashes.begin()) : SIZE_MAX;
        };

        size_t selected = find_chart(keep_selected);
        size_t preview = find_chart(keep_preview);
        selected_chart = nullptr;
        preview_audio_chart = preview != SIZE_MAX ? library.charts[preview].get() : nullptr;

        if (preview_audio_chart == nullptr) {
            stop_preview_audio();
        }

        apply_search(selected);

        if (visible.empty()) {
            field.reset();

            if (previous != nullptr) {
                previous->release_notes();
            }

            return;
        }

        select_chart(selected_chart_index, false);

        if (previous != nullptr && previous != selected_chart) {
            previous->release_notes();
        }
    }

    void SceneList::fill_difficulties() {
        if (library.difficulties.size() != library.size()) {
            library.difficulties.assign(library.size(), std::nullopt);
        }

        chunked_for(library.size(), [&](size_t index) {
            if (!library.difficulties[index]) {
                if (std::optional<double> cached = difficulty_cache->lookup(*library.charts[index], pitch)) {
                    library.difficulties[index] = (float)*cached;
                }
            }
        });
    }

    void SceneList::build_field_texts() {
        if (field_texts_version == library_version) {
            return;
        }

        artist_texts.assign(library.size(), std::string());
        title_texts.assign(library.size(), std::string());
        mapper_texts.assign(library.size(), std::string());

        chunked_for(library.size(), [&](size_t index) {
            const ChartInfo& info = library.charts[index]->info;
            artist_texts[index] = search_field_text(info, "artist");
            title_texts[index] = search_field_text(info, "title");
            mapper_texts[index] = search_field_text(info, "mapper");
        });

        field_texts_version = library_version;
    }

    void SceneList::retire_idle_charts() {
        if (retired_charts.empty()) {
            return;
        }

        std::vector<std::unique_ptr<Chart>> freed;

        {
            std::lock_guard<std::mutex> lock(difficulty_mutex);

            if (difficulty_busy || rate_busy > 0) {
                return;
            }

            freed.swap(retired_charts);
        }
    }

    void SceneList::save_difficulty_cache_later() {
        double now = now_ms();

        if (!difficulty_cache_loaded || now - difficulty_cache_saved_at < DIFFICULTY_SAVE_MS || (difficulty_cache_save.valid() && !is_ready(difficulty_cache_save))) {
            return;
        }

        difficulty_cache_saved_at = now;

        if (!difficulty_cache->is_dirty()) {
            return;
        }

        DifficultyCache* cache = difficulty_cache.get();
        difficulty_cache_save = std::async(std::launch::async, [cache] {
            std::string error;

            if (!cache->save(error)) {
                std::cerr << "Difficulty cache: " << error << std::endl;
            }
        });
    }

    void SceneList::apply_search(size_t keep_chart, bool narrow) {
        SearchQuery query = parse_search(search);
        bool narrowing = narrow && !sort_order_dirty && searched_query && searched_data_version == search_data_version && query_narrows(*searched_query, query);
        std::vector<size_t> candidates = narrowing ? std::move(visible) : std::vector<size_t>();
        visible.clear();
        selected_chart_index = 0;

        if (library.difficulties.size() != library.size()) {
            library.difficulties.assign(library.size(), std::nullopt);
        }

        if (!narrowing && sort_order_dirty) {
            build_sort_order();
        }

        bool wants_best = query.filters_on("played") || query.filters_on("grade");
        bool wants_favorite = query.filters_on("favorite");
        bool wants_fields = query.filters_on("artist") || query.filters_on("title") || query.filters_on("mapper");

        if (wants_fields) {
            build_field_texts();
        }

        const std::vector<size_t>& source = narrowing ? candidates : sort_order;
        std::vector<char> matched(source.size(), 0);

        chunked_for(source.size(), [&](size_t position) {
            size_t index = source[position];
            const Chart& chart = *library.charts[index];
            const LocalBest* best = wants_best ? LocalBests::lookup(library.hashes[index], pitch) : nullptr;
            std::optional<float> rating = get_difficulty(index);
            SearchSubject subject{
                chart.info,
                library.search_texts[index],
                library.note_counts[index],
                library.lengths_ms[index] / pitch / 1000.0,
                rating ? std::optional<double>(*rating) : std::nullopt,
                best != nullptr ? std::optional<double>(best->accuracy) : std::nullopt,
                index < library.bpms.size() && library.bpms[index].main > 0 ? std::optional<double>(library.bpms[index].main * pitch) : std::nullopt,
                wants_favorite && Favorites::contains(library.hashes[index]),
                wants_fields ? &artist_texts[index] : nullptr,
                wants_fields ? &title_texts[index] : nullptr,
                wants_fields ? &mapper_texts[index] : nullptr,
            };

            matched[position] = matches_search(query, subject) ? 1 : 0;
        });

        for (size_t position = 0; position < source.size(); position++) {
            if (matched[position]) {
                visible.push_back(source[position]);
            }
        }

        searched_query = std::move(query);
        searched_data_version = search_data_version;
        auto kept = std::find(visible.begin(), visible.end(), keep_chart);
        selected_chart_index = kept != visible.end() ? (int32_t)(kept - visible.begin()) : 0;
        list_scroll_offset = 0;
    }

    ListSort SceneList::current_sort() {
        uint8_t sort = SettingsManager::settings.list_sort;
        return sort < (uint8_t)ListSort::COUNT ? (ListSort)sort : ListSort::TITLE;
    }

    void SceneList::set_sort(ListSort sort) {
        if (sort == current_sort()) {
            return;
        }

        SettingsManager::settings.list_sort = (uint8_t)sort;
        SettingsManager::save();
        sort_order_dirty = true;
        refresh_search();
    }

    void SceneList::open_sort_menu() {
        overlays.open(std::make_unique<SortMenu>(skin, current_sort(), [this](ListSort sort) {
            set_sort(sort);
        }));
    }

    void SceneList::build_sort_order() {
        ListSort sort = current_sort();
        sort_order.resize(library.size());

        for (size_t index = 0; index < sort_order.size(); index++) {
            sort_order[index] = index;
        }

        sort_order_dirty = false;

        if (sort == ListSort::TITLE) {
            return;
        }

        std::vector<std::string> artists;
        std::vector<std::string> played;
        std::vector<double> accuracies;

        if (sort == ListSort::ARTIST) {
            artists.resize(library.size());

            for (size_t index = 0; index < library.size(); index++) {
                artists[index] = ascii_lowercase(library.charts[index]->info.artist);
            }
        }

        if (sort == ListSort::RECENTLY_PLAYED) {
            played.resize(library.size());

            for (size_t index = 0; index < library.size(); index++) {
                played[index] = LocalBests::last_played(library.hashes[index]);
            }
        }

        if (sort == ListSort::BEST_ACCURACY) {
            accuracies.assign(library.size(), -1.0);

            for (size_t index = 0; index < library.size(); index++) {
                const LocalBest* best = LocalBests::lookup(library.hashes[index], pitch);
                accuracies[index] = best != nullptr ? best->accuracy : -1.0;
            }
        }

        auto bpm = [&](size_t index) {
            return index < library.bpms.size() ? library.bpms[index].main : 0.0;
        };

        auto long_notes = [&](size_t index) {
            return index < library.long_note_shares.size() ? library.long_note_shares[index] : 0.0f;
        };

        auto added = [&](size_t index) {
            return index < library.added.size() ? library.added[index] : (int64_t)0;
        };

        std::stable_sort(sort_order.begin(), sort_order.end(), [&](size_t a, size_t b) {
            switch (sort) {
                case ListSort::ARTIST:
                    return artists[a] < artists[b];
                case ListSort::DIFFICULTY: {
                    std::optional<float> left = get_difficulty(a);
                    std::optional<float> right = get_difficulty(b);
                    return left && (!right || *left < *right);
                }
                case ListSort::BPM:
                    return bpm(a) > 0 && (bpm(b) <= 0 || bpm(a) < bpm(b));
                case ListSort::LENGTH:
                    return library.lengths_ms[a] < library.lengths_ms[b];
                case ListSort::DATE_ADDED:
                    return added(a) > added(b);
                case ListSort::KEY_COUNT:
                    return library.charts[a]->info.key_count < library.charts[b]->info.key_count;
                case ListSort::LONG_NOTES:
                    return long_notes(a) < long_notes(b);
                case ListSort::RECENTLY_PLAYED:
                    return played[a] > played[b];
                case ListSort::BEST_ACCURACY:
                    return accuracies[a] > accuracies[b];
                default:
                    return false;
            }
        });
    }

    size_t SceneList::selected_library_index() const {
        auto found = std::find_if(library.charts.begin(), library.charts.end(), [&](const std::unique_ptr<Chart>& chart) {
            return chart.get() == selected_chart;
        });

        return selected_chart != nullptr && found != library.charts.end() ? (size_t)(found - library.charts.begin()) : SIZE_MAX;
    }

    void SceneList::refresh_search() {
        apply_search(selected_library_index());
    }

    void SceneList::update_search() {
        std::string before = search;
        search += game->input().text();

        for (olc::Key key : game->input().pressed_keys()) {
            if (key == olc::Key::BACK && !search.empty()) {
                search.pop_back();

                while (!search.empty() && ((unsigned char)search.back() & 0xc0) == 0x80) {
                    search.pop_back();
                }
            }
        }

        if (game->input().take(actions::BACK)) {
            search.clear();
        }

        if (search == before) {
            return;
        }

        apply_search(selected_library_index(), true);

        if (!visible.empty() && library.charts[visible[selected_chart_index]].get() != selected_chart) {
            select_chart(selected_chart_index);
            list_scroll_offset = 0;
        }
    }

    void SceneList::draw_search(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();
        olc::Font& font = skin->display_font_small;
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t bar_height = StatusBar::height();
        int32_t padding = StatusBar::padding();
        int32_t baseline = (bar_height - metrics.size.y) / 2 - metrics.offset.y;

        window->FillRectDecal({0.0f, 0.0f}, {(float)screensize.x, (float)bar_height}, skin->color(SkinColor::STATUS_BAR_BACKGROUND));
        window->FillRectDecal({0.0f, (float)(bar_height - 1)}, {(float)screensize.x, 1.0f}, skin->color(SkinColor::STATUS_BAR_EDGE));
        int32_t right = screensize.x - padding - GameNascent::fps_counter_width();
        float hint_gap = Ui::layout_pxf("song_list.hint_gap");

        if (search.empty()) {
            draw_text(font, "Type to search", {padding, baseline}, skin->color(SkinColor::TEXT_FAINT));
        }
        else {
            std::string count = std::format("{} / {}", visible.size(), library.size());
            int32_t count_width = text_bounds(font, count).size.x;
            draw_text(font, search, {padding, baseline}, skin->color(SkinColor::TEXT));
            float clear_x = (float)padding + text_bounds(font, search).size.x + hint_gap;
            draw_hints(window, font, {{"Esc  clear", SPOT_SEARCH_CLEAR}}, {clear_x, (float)baseline}, hint_gap, skin->color(SkinColor::TEXT_FAINT), skin->color(SkinColor::TEXT), ui_spots);
            right -= count_width;
            draw_text(font, count, {right, baseline}, skin->color(visible.empty() ? SkinColor::TEXT_ERROR : SkinColor::TEXT_MUTED));
            right -= (int32_t)hint_gap;
        }

        if (background_work_opacity <= 0) {
            return;
        }

        std::string label = std::format("{}  {:.0f}%", background_work_label, background_work_fraction * 100.0f);
        draw_text(font, label, {right - text_bounds(font, label).size.x, baseline}, fade(skin->color(SkinColor::TEXT_FAINT), background_work_opacity));
        float work_height = std::max(1.0f, Ui::layout_pxf("song_list.background_work_bar_height"));
        ProgressBar work_bar({(float)screensize.x, work_height}, fade(skin->color(SkinColor::PROGRESS_TRACK), background_work_opacity));
        work_bar.draw(window, {0.0f, (float)bar_height - work_height}, background_work_fraction, fade(skin->accent(0.5), background_work_opacity));
    }

    void SceneList::download_recommended() {
        for (const RecommendedSong& song : RECOMMENDED_SONGS) {
            BeatmapDownloads::start(song.set_id, song.artist, song.title);
        }

        recommended_started = true;
        game->audio().play_effect(skin->get_random_menu_pluck());
    }

    void SceneList::draw_recommended(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = (int32_t)(screen.x * Ui::layout("song_list.empty_left_fraction"));
        int32_t y = (int32_t)(screen.y * Ui::layout("song_list.empty_top_fraction"));
        bool any_failed = false;
        bool any_downloading = false;
        bool all_started = true;
        olc::FontRect metrics = text_bounds(skin->display_font_small, "Hgjy");
        float row_height = Ui::layout_pxf("song_list.recommended_row_height");
        int32_t row_step = Ui::layout_px("song_list.recommended_row_height");
        int32_t status_offset = Ui::layout_px("song_list.recommended_status_offset");

        draw_text(skin->display_font_large, "Your library is empty", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::layout_px("song_list.empty_title_gap");
        draw_text(skin->display_font_small, "Download these recommended songs to get started.", {x, y}, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::layout_px("song_list.empty_subtitle_gap");

        for (size_t index = 0; index < std::size(RECOMMENDED_SONGS); index++) {
            const RecommendedSong& song = RECOMMENDED_SONGS[index];
            std::shared_ptr<BeatmapDownloads::Download> download = BeatmapDownloads::find(song.set_id);
            std::string status;
            olc::Pixel status_color = skin->color(SkinColor::TEXT_MUTED);

            if (download == nullptr) {
                all_started = false;
            }
            else {
                switch (download->state.load()) {
                    case BeatmapDownloads::State::DOWNLOADING:
                        status = std::format("{:.0f}%", download->progress.load() * 100.0);
                        any_downloading = true;
                        break;
                    case BeatmapDownloads::State::EXTRACTING:
                        status = "Extracting";
                        break;
                    case BeatmapDownloads::State::DONE:
                        status = "Downloaded";
                        status_color = skin->color(SkinColor::POSITIVE);
                        break;
                    case BeatmapDownloads::State::FAILED:
                        status = fit_text(skin->display_font_small, "Failed: " + download->error(), screen.x - x * 2 - status_offset);
                        status_color = skin->color(SkinColor::TEXT_ERROR);
                        any_failed = true;
                        break;
                }
            }

            draw_text(skin->display_font_small, std::format("{} - {}", song.artist, song.title), {x, y}, skin->color(SkinColor::TEXT));
            draw_text(skin->display_font_small, status, {x + status_offset, y}, status_color);
            float row_top = (float)(y + metrics.offset.y) - (row_height - (float)metrics.size.y) / 2.0f;
            float row_width = (float)(status_offset + text_bounds(skin->display_font_small, "Extracting").size.x);
            ui_spots.add(Rect{{(float)x, row_top}, {row_width, row_height}}, SPOT_RECOMMENDED_FIRST + (int32_t)index);
            y += row_step;
        }

        y += Ui::layout_px("song_list.empty_hints_gap");

        Hint download = !all_started ? Hint{"Enter:  download recommended songs", SPOT_DOWNLOAD}
            : any_failed             ? Hint{"Enter:  retry failed downloads", SPOT_DOWNLOAD}
                                     : Hint{"Downloading...", -1};
        std::vector<Hint> hints = {download, {"F5:  browse charts", SPOT_BROWSE}};

        if (any_downloading) {
            hints.push_back({"Right-click a song:  cancel"});
        }

        draw_hints(window, skin->display_font_small, hints, {(float)x, (float)y}, Ui::layout_pxf("song_list.hint_gap"), skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), ui_spots);
    }

    void SceneList::select_chart_by_hash(std::string hash, std::filesystem::path path, int playing_audio_id) {
        pending_selection = std::move(hash);
        pending_selection_path = std::move(path);
        pending_audio_id = playing_audio_id;
    }

    const std::unordered_set<std::string>& SceneList::get_local_hashes() const {
        return local_hashes;
    }

    void SceneList::draw(olc::PixelGameEngine* window) {
        row_spots.clear();
        ui_spots.clear();
        draw_list(window);
        draw_search(window);
        status_bar.draw(window, skin);
    }

    static std::optional<std::filesystem::path> pack_folder_of(const Chart& chart) {
        std::error_code ec;
        std::filesystem::path song = std::filesystem::weakly_canonical(chart.file_path.parent_path(), ec);
        std::filesystem::path songs = std::filesystem::weakly_canonical(SettingsManager::song_directory(), ec);

        if (ec || song.empty() || song.parent_path() == songs || song.parent_path().parent_path() != songs) {
            return std::nullopt;
        }

        return song.parent_path();
    }

    size_t SceneList::pack_chart_count() const {
        if (selected_chart == nullptr || !pack_folder_of(*selected_chart)) {
            return 0;
        }

        std::filesystem::path pack = selected_chart->file_path.parent_path().parent_path();
        return (size_t)std::count_if(library.charts.begin(), library.charts.end(), [&](const std::unique_ptr<Chart>& chart) {
            return chart->file_path.parent_path().parent_path() == pack;
        });
    }

    size_t SceneList::song_chart_count() const {
        if (selected_chart == nullptr) {
            return 0;
        }

        std::filesystem::path folder = selected_chart->file_path.parent_path();
        return (size_t)std::count_if(library.charts.begin(), library.charts.end(), [&](const std::unique_ptr<Chart>& chart) {
            return chart->file_path.parent_path() == folder;
        });
    }

    void SceneList::open_chart_menu() {
        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        overlays.open(std::make_unique<ChartMenu>(skin, *selected_chart, song_chart_count(), pack_chart_count(), Favorites::contains(selected_hash()), [this](ChartAction action) {
            handle_chart_action(action);
        }));
    }

    void SceneList::handle_chart_action(ChartAction action) {
        if (selected_chart == nullptr || visible.empty()) {
            return;
        }

        switch (action) {
            case ChartAction::WATCH_REPLAY:
                open_replay_menu();
                break;
            case ChartAction::TOGGLE_FAVORITE:
                toggle_favorite();
                break;
            case ChartAction::OPEN_WEBSITE:
                open_chart_website();
                break;
            case ChartAction::OPEN_FOLDER:
                if (!open_folder(selected_chart->file_path.parent_path())) {
                    game->notify("Could not open the chart's folder", true);
                }

                break;
            default:
                delete_charts(action);
                break;
        }
    }

    void SceneList::open_replay_menu() {
        overlays.open(std::make_unique<ReplayMenu>(skin, *selected_chart, selected_hash(), pitch, [this](Replay replay, std::string label) {
            watch_replay(std::move(replay), std::move(label));
        }));
    }

    void SceneList::watch_replay(Replay replay, std::string label) {
        if (selected_chart == nullptr) {
            return;
        }

        if (!replay.chart.gameplay_hash.empty() && replay.chart.gameplay_hash != selected_hash()) {
            game->notify("That replay was recorded on a different version of this chart", true);
            return;
        }

        selected_chart->set_pitch(replay.rate);
        auto play = std::make_unique<ScenePlay>(selected_chart, skin, std::move(replay), std::move(label));
        play->use_background(background_for(*selected_chart));
        game->scenes().push(std::move(play));
    }

    void SceneList::toggle_leaderboard_mode() {
        LeaderboardMode mode = leaderboards.get_mode() == LeaderboardMode::ACCURACY ? LeaderboardMode::PERFORMANCE : LeaderboardMode::ACCURACY;
        leaderboards.set_mode(mode);
        SettingsManager::settings.list_leaderboard_mode = (uint8_t)mode;
        SettingsManager::save();

        if (!show_leaderboards) {
            run_status_action(StatusAction::TOGGLE_SCORES, false);
            return;
        }

        leaderboards.show_now();
        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
    }

    void SceneList::open_leaderboard_play(LeaderboardPick pick) {
        if (selected_chart == nullptr) {
            return;
        }

        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));

        if (pick.path) {
            std::string error;
            std::optional<Replay> replay = load_replay(*pick.path, error);

            if (!replay) {
                game->notify(std::format("Couldn't open the replay: {}", error), true);
                return;
            }

            show_replay_results(std::move(*replay), std::move(pick.label), 0);
            return;
        }

        if (pick.play_id <= 0 || replay_download) {
            return;
        }

        replay_download = ServerApi::replay(pick.play_id);
        replay_download_label = std::move(pick.label);
        replay_download_hash = selected_hash();
        replay_download_id = pick.play_id;
        game->notify("Downloading the replay...");
    }

    void SceneList::poll_replay_download() {
        if (!replay_download || !is_ready(*replay_download)) {
            return;
        }

        ApiResult<Replay> result = replay_download->get();
        replay_download.reset();

        if (!result.ok()) {
            game->notify(std::format("Couldn't download the replay: {}", result.error()), true);
            return;
        }

        if (!active || selected_chart == nullptr || selected_hash() != replay_download_hash) {
            return;
        }

        show_replay_results(std::move(*result.value), std::move(replay_download_label), replay_download_id);
    }

    void SceneList::show_replay_results(Replay replay, std::string label, int64_t play_id) {
        if (!replay.chart.gameplay_hash.empty() && replay.chart.gameplay_hash != selected_hash()) {
            game->notify("That replay was recorded on a different version of this chart", true);
            return;
        }

        if (!(replay.rate > 0.0f) || !std::isfinite(replay.rate)) {
            game->notify("That replay has an invalid rate", true);
            return;
        }

        double difficulty = difficulty_cache->difficulty(*selected_chart, replay.rate);
        selected_chart->set_pitch(replay.rate);
        AttemptSummary summary = summarize_attempt(simulate_replay(*selected_chart, replay), difficulty);
        summary.pause_count = (uint32_t)replay.pauses.size();

        auto results = std::make_unique<SceneResults>(skin, selected_chart, std::move(summary), std::future<ApiResult<Submission>>(), -1, false, label);
        results->enable_replay(selected_chart, std::move(replay), std::move(label), background_for(*selected_chart));
        results->set_play_id(play_id);
        game->scenes().push(std::move(results));
    }

    void SceneList::toggle_favorite() {
        bool added = Favorites::toggle(selected_hash());
        game->notify(added ? std::format("Added {} to favorites", selected_chart->info.title) : std::format("Removed {} from favorites", selected_chart->info.title));
        search_data_version++;
        refresh_search();

        if (!visible.empty()) {
            select_chart(selected_chart_index, false);
        }
    }

    void SceneList::open_chart_website() {
        if (!open_url(ServerApi::chart_page_url(selected_chart->gameplay_hash(), pitch))) {
            game->notify("Could not open the browser", true);
        }
    }

    void SceneList::delete_charts(ChartAction action) {
        std::error_code ec;
        std::filesystem::path chart_path = selected_chart->file_path;
        bool whole_pack = action != ChartAction::DELETE_CHART;

        if (ChartFolders::in_linked_folder(chart_path)) {
            game->notify("That chart is in a linked folder, so Nascent won't delete it", true);
            return;
        }

        if (!whole_pack && holds_several_charts(chart_path)) {
            game->notify("That chart shares its file with the song's other difficulties, so delete the whole song instead", true);
            return;
        }

        std::filesystem::path folder = std::filesystem::weakly_canonical(chart_path.parent_path(), ec);
        std::filesystem::path songs = std::filesystem::weakly_canonical(SettingsManager::song_directory(), ec);

        if (ec || folder.empty() || (folder.parent_path() != songs && folder.parent_path().parent_path() != songs)) {
            game->notify("That chart isn't in your songs folder, so it wasn't deleted", true);
            return;
        }

        if (action == ChartAction::DELETE_PACK) {
            std::optional<std::filesystem::path> pack = pack_folder_of(*selected_chart);

            if (!pack) {
                game->notify("That chart isn't part of a pack", true);
                return;
            }

            folder = *pack;
        }

        std::vector<bool> removed(library.size(), false);
        size_t removed_count = 0;
        std::u8string target = folder_key(action == ChartAction::DELETE_PACK ? chart_path.parent_path().parent_path() : chart_path.parent_path());

        auto parent_of = [](std::u8string_view folder) {
            size_t separator = folder.find_last_of(u8"/\\");
            return separator == std::u8string_view::npos ? std::u8string_view() : folder.substr(0, separator);
        };

        for (size_t index = 0; index < library.size(); index++) {
            std::u8string_view chart_folder = library.folders[index];
            bool inside = action == ChartAction::DELETE_PACK ? parent_of(chart_folder) == target : chart_folder == target;
            removed[index] = whole_pack ? inside : library.charts[index].get() == selected_chart;
            removed_count += removed[index] ? 1 : 0;
        }

        std::string name = whole_pack ? path_utf8(folder.filename()) : std::format("{} [{}]", selected_chart->info.title, selected_chart->info.difficulty_name);

        if (whole_pack) {
            std::filesystem::remove_all(folder, ec);
        }
        else {
            std::filesystem::remove(chart_path, ec);
        }

        if (ec) {
            game->notify(std::format("Could not delete {}: {}", name, ec.message()), true);
            return;
        }

        if (!whole_pack) {
            bool charts_left = false;

            for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
                charts_left = charts_left || is_library_chart_file(entry.path());
            }

            if (!ec && !charts_left) {
                std::filesystem::remove_all(folder, ec);
            }
        }

        Chart* next = nullptr;

        for (size_t step = 1; step <= visible.size() && next == nullptr; step++) {
            size_t candidate = visible[(selected_chart_index + step) % visible.size()];

            if (!removed[candidate]) {
                next = library.charts[candidate].get();
            }
        }

        std::vector<std::unique_ptr<Chart>> dropped;
        ChartLibrary remaining = library.without(removed, dropped);
        selected_chart = next;
        apply_library(std::move(remaining), std::move(dropped));

        if (library_loading) {
            library_reload_pending = true;
        }

        BeatmapDownloads::forget_missing();
        game->notify(whole_pack ? std::format("Deleted {} ({} charts)", name, removed_count) : std::format("Deleted {}", name));
    }

    float SceneList::list_position() const {
        return visible.size() > 1 ? (float)selected_chart_index / (float)(visible.size() - 1) : 0.0f;
    }

    void SceneList::draw_list_scrollbar(olc::PixelGameEngine* window, int32_t charts_per_screen) {
        if (visible.size() < 2) {
            return;
        }

        olc::vi2d screensize = window->GetScreenSize();
        float margin = Ui::layout_pxf("song_list.scrollbar_margin");
        float track_width = std::max(1.0f, Ui::layout_pxf("song_list.scrollbar_width"));
        float track_x = Ui::layout_pxf("song_list.scrollbar_gap");
        float track_top = (float)StatusBar::height() + margin;
        float track_height = (float)(screensize.y - StatusBar::height() * 2) - margin * 2.0f;
        list_scrollbar.layout({{track_x, track_top}, {track_width, track_height}}, (float)charts_per_screen / (float)visible.size());
        list_scrollbar.draw(window, list_position(), skin->color(SkinColor::SCROLLBAR_TRACK), skin->color(SkinColor::SCROLLBAR_THUMB));
    }

    void SceneList::draw_list(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();
        list_scrollbar.hide();

        if (visible.empty()) {
            if (library.charts.empty() && (!library_loading || recommended_started)) {
                draw_recommended(window);
                return;
            }

            draw_text(skin->display_font_medium, library.charts.empty() ? std::string("Loading charts...") : std::string("No charts match your search"), {(int32_t)(screensize.x * Ui::layout("song_list.margin_fraction")), screensize.y / 2}, skin->color(SkinColor::TEXT_MUTED));
            return;
        }

        float beat_progress = selected_chart->beat_progress_at(preview_position);
        uint8_t background_brightening = Ui::layout("song_list.background_beat_flash") * std::clamp((1.0 - beat_progress * 3 + std::max(0.0, beat_progress - 0.75) * 4), 0.0, 1.0);

        olc::vf2d rect[4] = {
            {0.0f, 0.0f},
            {0.0f, screensize.y - 1.0f},
            {screensize.x - 1.0f, screensize.y - 1.0f},
            {screensize.x - 1.0f, 0.0f}
        };

        olc::Pixel background_tint = skin->color(SkinColor::BACKGROUND_TINT);

        for (uint8_t* channel : {&background_tint.r, &background_tint.g, &background_tint.b}) {
            *channel = (uint8_t)std::min(255, *channel + background_brightening);
        }

        auto cover_scale = [&](const MipDecal& image) {
            olc::vf2d size = image.size();
            return std::max(screensize.x / std::max(1.0f, size.x), screensize.y / std::max(1.0f, size.y));
        };

        if (background_image) {
            window->DrawWarpedDecal(background_image.pick(cover_scale(background_image)).decal, rect, fade(background_tint, 1 - crossfade_timer));
        }

        if (old_background_image) {
            window->DrawWarpedDecal(old_background_image.pick(cover_scale(old_background_image)).decal, rect, fade(background_tint, crossfade_timer));
        }

        int32_t row_height = Ui::layout_px("song_list.row_height");
        const int32_t charts_per_screen = screensize.y / std::max(1, row_height);
        float list_margin = (float)(screensize.x * Ui::layout("song_list.margin_fraction"));

        if (widest_grade < 0) {
            widest_grade = 0;

            for (Grade grade : {Grade::X, Grade::S, Grade::A, Grade::B, Grade::C, Grade::D, Grade::E}) {
                widest_grade = std::max(widest_grade, (float)text_bounds(skin->display_font_large, grade_name(grade)).size.x);
            }
        }

        float grade_gap = Ui::layout_pxf("song_list.grade_gap");
        float grade_indent = std::max(0.0f, Ui::layout_pxf("song_list.grade_screen_margin") + widest_grade + grade_gap - list_margin);
        float bar_width = Ui::layout_pxf("song_list.difficulty_bar_width");
        olc::Font& detail_font = skin->display_font_small;
        float detail_gap = Ui::layout_pxf("song_list.detail_gap");
        float row_hit_above = Ui::layout_pxf("song_list.row_hit_above");
        double curve_reach = Ui::layout("song_list.curve_reach");
        double curve_softness = Ui::layout("song_list.curve_softness");
        float favorite_radius = Ui::layout_pxf("song_list.favorite_radius");
        float favorite_gap = Ui::layout_pxf("song_list.favorite_gap");
        float favorite_ratio = (float)Ui::layout("song_list.favorite_inner_ratio");
        double idle_opacity = Ui::layout("song_list.difficulty_idle_opacity");
        float bar_max = (float)Ui::layout("song_list.difficulty_bar_max");
        uint8_t idle_bar_alpha = (uint8_t)Ui::layout("song_list.difficulty_bar_idle_alpha");
        int32_t details_offset = Ui::layout_px("song_list.details_offset");
        float bar_offset = Ui::layout_pxf("song_list.difficulty_bar_offset");
        int32_t stats_offset = Ui::layout_px("song_list.stats_offset");
        float grade_center_offset = Ui::layout_pxf("song_list.grade_center_offset");

        if (favorite_ratio != favorite_star_ratio) {
            favorite_star = StarSprite(favorite_ratio);
            favorite_star_ratio = favorite_ratio;
        }

        auto widest = [&](std::initializer_list<std::string_view> samples) {
            int32_t width = 0;

            for (std::string_view sample : samples) {
                width = std::max(width, text_bounds(detail_font, sample).size.x);
            }

            return (float)width;
        };

        float keys_column = bar_width + detail_gap;
        float difficulty_column = keys_column + widest({"10K", "44K", "88K"}) + detail_gap;
        float length_column = difficulty_column + widest({"00.00", "44.44", "88.88"}) + detail_gap;
        float bpm_column = length_column + widest({"00:00", "44:44", "88:88"}) + detail_gap;
        const ProgressBar difficulty_bar({bar_width, std::max(1.0f, Ui::layout_pxf("song_list.difficulty_bar_height"))}, skin->color(SkinColor::PROGRESS_TRACK));
        wanted_difficulties.clear();

        if (show_leaderboards) {
            leaderboards.draw(window, skin, selected_hash(), pitch, side_panel_position(screensize), side_panel_size(screensize));
            preview_seek.hide();
        }
        else {
            field->draw(window);
            draw_preview_seek(window);
        }

        for (int32_t index = selected_chart_index - (charts_per_screen / 2 + 5); index < selected_chart_index + (charts_per_screen / 2 + 5); index++) {
            size_t chart_index = visible[positive_modulo(index, visible.size())];
            Chart* chart = library.charts[chart_index].get();
            float y = (float)screensize.y / 2 + (index - selected_chart_index) * row_height + list_scroll_offset;

            if (y < -row_height || y > screensize.y + row_height) {
                continue;
            }

            double distance = std::abs((float)screensize.y / 2 - y) / Ui::scale();
            float x = list_margin + grade_indent + Ui::pxf(std::pow(std::max(curve_reach - distance, 0.0) / curve_softness, 2));
            Rect row_area = {{0.0f, y - row_hit_above}, {(float)side_panel_position(screensize).x, (float)row_height}};
            row_spots.add(row_area, ROW_SPOT_CENTER + (index - selected_chart_index));
            bool hovered = index != selected_chart_index && row_area.contains(window->GetMousePos());
            olc::Pixel text_color = skin->color(index == selected_chart_index || hovered ? SkinColor::TEXT : SkinColor::TEXT_MUTED);

            draw_text(skin->display_font_medium, chart->info.title, {(int)x, (int)y}, text_color);

            if (Favorites::contains(library.hashes[chart_index])) {
                olc::FontRect title_bounds = text_bounds(skin->display_font_medium, chart->info.title);
                olc::vf2d star = {x + title_bounds.offset.x + title_bounds.size.x + favorite_gap + favorite_radius, y + title_bounds.offset.y + title_bounds.size.y / 2.0f};
                olc::Pixel star_color = skin->color(SkinColor::ATTENTION);
                favorite_star.draw(window, star, favorite_radius, index == selected_chart_index ? star_color : fade(star_color, idle_opacity));
            }
            draw_text(skin->display_font_small, library.details[chart_index], {(int)x, (int)y + details_offset}, text_color);

            std::optional<float> rating = get_difficulty(chart_index);

            if (!rating) {
                wanted_difficulties.push_back(chart_index);
            }

            float fill = rating ? std::clamp(*rating / bar_max, 0.0f, 1.0f) : 0.0f;
            olc::Pixel rating_color = rating ? skin->difficulty_color(*rating) : skin->color(SkinColor::TEXT_MUTED);
            olc::Pixel bar_color = rating_color;
            bar_color.a = index == selected_chart_index ? 255 : idle_bar_alpha;
            difficulty_bar.draw(window, {x, y + bar_offset}, fill, bar_color);
            draw_text(skin->display_font_small, std::format("{}K", chart->info.key_count), {(int)(x + keys_column), (int)y + stats_offset}, text_color);

            if (rating) {
                olc::Pixel number_color = index == selected_chart_index ? rating_color : fade(rating_color, idle_opacity);
                draw_text(skin->display_font_small, std::format("{:.2f}", *rating), {(int)(x + difficulty_column), (int)y + stats_offset}, number_color);
            }

            int64_t length_ms = std::llround(library.lengths_ms[chart_index] / pitch);
            draw_text(skin->display_font_small, std::format("{}:{:02}", length_ms / 60000, (length_ms / 1000) % 60), {(int)(x + length_column), (int)y + stats_offset}, text_color);

            if (chart_index < library.bpms.size() && library.bpms[chart_index].main > 0) {
                const BpmRange& bpm = library.bpms[chart_index];
                long low = std::lround(bpm.min * pitch);
                long high = std::lround(bpm.max * pitch);
                std::string bpm_text = low == high ? std::format("{} BPM", std::lround(bpm.main * pitch)) : std::format("{}-{} BPM", low, high);
                draw_text(skin->display_font_small, bpm_text, {(int)(x + bpm_column), (int)y + stats_offset}, text_color);
            }

            const LocalBest* best = LocalBests::lookup(library.hashes[chart_index], pitch);

            if (best != nullptr) {
                Grade grade = grade_for_accuracy(best->accuracy);
                std::string_view grade_text = grade_name(grade);
                olc::FontRect grade_bounds = text_bounds(skin->display_font_large, grade_text);
                float grade_center_y = y + grade_center_offset;
                skin->draw_grade(grade, skin->display_font_large, {x - grade_gap - grade_bounds.size.x - grade_bounds.offset.x, grade_center_y - grade_bounds.size.y / 2.0f - grade_bounds.offset.y});
            }
        }

        draw_list_scrollbar(window, charts_per_screen);
        request_difficulties();
    }

    std::optional<float> SceneList::get_difficulty(size_t index) const {
        return index < library.difficulties.size() ? library.difficulties[index] : std::nullopt;
    }

    void SceneList::difficulty_loop() {
        std::unique_lock<std::mutex> lock(difficulty_mutex);

        while (true) {
            difficulty_cv.wait(lock, [this] { return difficulty_stopping || !difficulty_jobs.empty(); });

            if (difficulty_stopping) {
                return;
            }

            DifficultyJob job = difficulty_jobs.front();
            difficulty_jobs.erase(difficulty_jobs.begin());
            difficulty_busy = true;
            lock.unlock();

            float difficulty = (float)library_difficulty(*difficulty_cache, library_cache.get(), *job.chart, job.pitch, false).value_or(0.0);

            lock.lock();
            difficulty_busy = false;
            difficulty_results.push_back({job.generation, job.index, difficulty});
            difficulty_cv.notify_all();
        }
    }

    void SceneList::request_difficulties() {
        {
            std::lock_guard<std::mutex> lock(difficulty_mutex);
            difficulty_jobs.clear();

            for (size_t index : wanted_difficulties) {
                difficulty_jobs.push_back({difficulty_generation, index, library.charts[index].get(), pitch});
            }
        }

        if (!wanted_difficulties.empty()) {
            difficulty_cv.notify_all();
        }
    }

    void SceneList::cancel_difficulties(bool wait) {
        rate_interrupted = true;
        std::unique_lock<std::mutex> lock(difficulty_mutex);
        difficulty_generation++;
        difficulty_jobs.clear();
        difficulty_results.clear();

        if (wait) {
            rate_charts.clear();
            rate_next = 0;
            difficulty_cv.wait(lock, [this] { return !difficulty_busy && rate_busy == 0; });
        }
    }

    void SceneList::rate_loop() {
        std::unique_lock<std::mutex> lock(difficulty_mutex);

        while (true) {
            difficulty_cv.wait(lock, [this] { return difficulty_stopping || (!rate_paused && rate_next < rate_charts.size() * rate_pitches.size()); });

            if (difficulty_stopping) {
                return;
            }

            size_t first = rate_next;
            size_t last = std::min(rate_charts.size() * rate_pitches.size(), first + RATE_BATCH);
            std::vector<std::tuple<size_t, const Chart*, float>> batch;

            for (size_t item = first; item < last; item++) {
                batch.emplace_back(item % rate_charts.size(), rate_charts[item % rate_charts.size()], rate_pitches[item / rate_charts.size()]);
            }

            rate_next = last;
            uint64_t generation = difficulty_generation;
            float current = rate_current_pitch;
            rate_busy++;
            lock.unlock();

            std::vector<DifficultyResult> computed;
            size_t stopped = last;

            for (size_t position = 0; position < batch.size(); position++) {
                auto [index, chart, rate] = batch[position];

                if (difficulty_cache->lookup(*chart, rate)) {
                    continue;
                }

                if (rate_interrupted) {
                    stopped = first + position;
                    break;
                }

                std::optional<double> value = library_difficulty(*difficulty_cache, library_cache.get(), *chart, rate, false);

                if (value && rate == current) {
                    computed.push_back({generation, index, (float)*value});
                }
            }

            lock.lock();
            rate_busy--;

            if (stopped < last && generation == difficulty_generation) {
                rate_next = std::min(rate_next, stopped);
            }

            difficulty_results.insert(difficulty_results.end(), computed.begin(), computed.end());
            difficulty_cv.notify_all();
        }
    }

    void SceneList::queue_all_rates() {
        std::vector<float> pitches;

        for (int32_t step = (int32_t)std::lround(PITCH_MINIMUM * PITCH_STEPS_PER_UNIT); step <= (int32_t)std::lround(PITCH_MAXIMUM * PITCH_STEPS_PER_UNIT); step++) {
            pitches.push_back((float)step / (float)PITCH_STEPS_PER_UNIT);
        }

        std::stable_sort(pitches.begin(), pitches.end(), [this](float a, float b) {
            return std::abs(a - pitch) < std::abs(b - pitch);
        });

        {
            std::lock_guard<std::mutex> lock(difficulty_mutex);
            rate_charts.clear();

            for (const std::unique_ptr<Chart>& chart : library.charts) {
                rate_charts.push_back(chart.get());
            }

            rate_pitches = std::move(pitches);
            rate_current_pitch = pitch;
            rate_next = 0;
            rate_interrupted = rate_paused;
        }

        difficulty_cv.notify_all();
    }

    void SceneList::pause_rates(bool paused) {
        {
            std::lock_guard<std::mutex> lock(difficulty_mutex);
            rate_paused = paused;
            rate_interrupted = paused;
        }

        difficulty_cv.notify_all();
    }

    void SceneList::collect_difficulties() {
        std::vector<DifficultyResult> results;
        {
            std::lock_guard<std::mutex> lock(difficulty_mutex);
            results.swap(difficulty_results);
        }

        if (library.difficulties.size() != library.size()) {
            library.difficulties.assign(library.size(), std::nullopt);
        }

        bool changed = false;

        for (const DifficultyResult& result : results) {
            if (result.generation == difficulty_generation && result.index < library.difficulties.size() && library.difficulties[result.index] != result.difficulty) {
                library.difficulties[result.index] = result.difficulty;
                changed = true;
            }
        }

        if (changed) {
            search_data_version++;
            difficulty_refresh_pending = difficulty_refresh_pending || current_sort() == ListSort::DIFFICULTY || (searched_query && searched_query->filters_on("difficulty"));
        }
    }

    std::optional<SceneList::BackgroundWork> SceneList::background_work() {
        if (library_loading) {
            static constexpr std::array<std::string_view, (size_t)LibraryLoadStage::DONE + 1> STAGE_LABELS = {
                "Finding charts",
                "Reading charts",
                "Calculating difficulties",
                "Saving the library",
                "Sorting charts",
                "Finishing up",
                "Finishing up",
            };

            size_t stage = std::min<size_t>(library_load_progress.stage, STAGE_LABELS.size() - 1);
            size_t total = library_load_progress.total;
            float fraction = total == 0 ? 0.0f : std::min(1.0f, (float)library_load_progress.done / (float)total);
            return BackgroundWork{std::string(STAGE_LABELS[stage]), fraction};
        }

        if (std::optional<BeatmapDownloads::Activity> downloads = BeatmapDownloads::activity()) {
            size_t count = downloads->songs + downloads->packs;
            std::string label;

            if (count == 1) {
                std::string name = downloads->name;

                if (name.size() > DOWNLOAD_NAME_LIMIT) {
                    name.resize(DOWNLOAD_NAME_LIMIT);

                    while (!name.empty() && ((unsigned char)name.back() & 0xc0) == 0x80) {
                        name.pop_back();
                    }

                    if (!name.empty() && (unsigned char)name.back() >= 0xc0) {
                        name.pop_back();
                    }

                    name += "...";
                }

                label = std::format("{} {}", downloads->installing ? "Installing" : "Downloading", name.empty() ? std::string(downloads->packs > 0 ? "a pack" : "a song") : name);
            }
            else if (downloads->packs == 0) {
                label = std::format("Downloading {} songs", count);
            }
            else if (downloads->songs == 0) {
                label = std::format("Downloading {} packs", count);
            }
            else {
                label = std::format("Downloading {} songs and packs", count);
            }

            return BackgroundWork{std::move(label), (float)downloads->progress};
        }

        std::lock_guard<std::mutex> lock(difficulty_mutex);
        size_t total = rate_charts.size() * rate_pitches.size();
        size_t finished = rate_next - rate_busy;

        if (total == 0 || finished >= total) {
            return std::nullopt;
        }

        return BackgroundWork{"Calculating difficulties for every rate", (float)finished / (float)total};
    }

    void SceneList::update_background_work(float elapsed_time) {
        std::optional<BackgroundWork> work = background_work();
        background_work_time = work ? background_work_time + elapsed_time : 0.0f;

        if (work) {
            bool restarted = work->label != background_work_label || work->fraction < background_work_fraction;
            background_work_fraction = restarted ? work->fraction : background_work_fraction + (work->fraction - background_work_fraction) * std::min(1.0f, elapsed_time * (float)Ui::layout("song_list.background_work_bar_speed"));
            background_work_label = std::move(work->label);
        }

        float target = work && background_work_time >= (float)Ui::layout("song_list.background_work_delay") ? 1.0f : 0.0f;
        float step = elapsed_time * (float)Ui::layout("song_list.background_work_fade_speed");
        background_work_opacity = target > background_work_opacity ? std::min(target, background_work_opacity + step) : std::max(target, background_work_opacity - step);
    }

    void SceneList::set_pitch(float pitch) {
        this->pitch = snap_pitch(pitch);
        status_bar.set_rate(this->pitch);
        cancel_difficulties(false);
        queue_all_rates();
        library.difficulties.assign(library.size(), std::nullopt);
        fill_difficulties();
        search_data_version++;

        if (current_sort() == ListSort::DIFFICULTY || current_sort() == ListSort::BEST_ACCURACY) {
            sort_order_dirty = true;
        }

        refresh_search();

        if (!visible.empty()) {
            select_chart(selected_chart_index);
        }
    }
}
