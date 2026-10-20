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
#include <utility>

namespace nascent {
    struct RecommendedSong {
        int64_t set_id;
        const char* artist;
        const char* title;
    };

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
        difficulty_cache->load();

        background_loader = std::thread([this] {
            background_load_loop();
        });

        difficulty_worker = std::thread([this] {
            difficulty_loop();
        });

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

        std::string error;

        if (!difficulty_cache->save(error)) {
            std::cerr << "Difficulty cache: " << error << std::endl;
        }
    }

    void SceneList::on_create() {
        if (!pending_selection_path.empty()) {
            std::string error;
            std::unique_ptr<Chart> chart = Chart::load(pending_selection_path, pitch, error);

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
        stop_preview_audio();
        load_generation++;
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
        leaderboards.update(elapsed_time, show_leaderboards && selected_chart != nullptr && !visible.empty(), selected_hash(), pitch);

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
            run_status_action(StatusAction::TOGGLE_SCORES, false);
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

    void SceneList::select_chart(int32_t index, bool sound) {
        selected_chart = library.charts[visible[index]].get();
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

        field = std::make_unique<EntityField>(selected_chart, skin, olc::vd2d{(double)position.x, 0.0}, olc::vd2d{(double)size.x, (double)screen.y});
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
            background_image = MipDecal(std::move(result.levels));
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
        library_load_progress.done = 0;
        library_load_progress.total = 0;
        std::vector<std::filesystem::path> song_directories = ChartFolders::all();
        float load_pitch = pitch;
        DifficultyCache* cache = difficulty_cache.get();

        library_loader = std::thread([this, song_directories, load_pitch, cache] {
            ChartLibrary loaded = load_chart_library(song_directories, load_pitch, *cache, &library_load_progress);
            std::lock_guard<std::mutex> lock(library_load_mutex);
            completed_library_load = std::move(loaded);
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

        std::optional<ChartLibrary> loaded;
        {
            std::lock_guard<std::mutex> lock(library_load_mutex);
            loaded = std::exchange(completed_library_load, std::nullopt);
        }

        if (!loaded) {
            return;
        }

        library_loader.join();
        library_loading = false;

        if (!LocalBests::is_backfilled()) {
            LocalBests::backfill(LocalReplays::gameplay_hash_by_folder(loaded->hashes), LocalReplays::directory());
        }

        apply_library(std::move(*loaded));
        library_loaded = true;

        if (std::exchange(library_reload_pending, false)) {
            start_library_load();
        }
    }

    void SceneList::apply_library(ChartLibrary loaded) {
        std::string keep_selected = selected_hash();
        std::string keep_preview = preview_audio_chart != nullptr ? preview_audio_chart->gameplay_hash() : std::string();

        cancel_difficulties(true);
        library = std::move(loaded);
        local_hashes = std::unordered_set<std::string>(library.hashes.begin(), library.hashes.end());

        if (library.pitch != pitch) {
            library.difficulties.assign(library.size(), std::nullopt);
        }

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
            return;
        }

        select_chart(selected_chart_index, false);
    }

    void SceneList::apply_search(size_t keep_chart) {
        SearchQuery query = parse_search(search);
        visible.clear();
        selected_chart_index = 0;

        if (library.difficulties.size() != library.size()) {
            library.difficulties.assign(library.size(), std::nullopt);
        }

        if (query.filters_on("difficulty") || current_sort() == ListSort::DIFFICULTY) {
            parallel_for(library.size(), [&](size_t index) {
                if (!library.difficulties[index]) {
                    library.difficulties[index] = (float)difficulty_cache->difficulty(*library.charts[index], pitch);
                }
            });
        }

        for (size_t index = 0; index < library.size(); index++) {
            const Chart& chart = *library.charts[index];
            const LocalBest* best = LocalBests::lookup(library.hashes[index], pitch);
            std::optional<float> rating = get_difficulty(index);
            SearchSubject subject{
                chart.info,
                library.search_texts[index],
                chart.hits.size(),
                library.lengths_ms[index] / pitch / 1000.0,
                rating ? std::optional<double>(*rating) : std::nullopt,
                best != nullptr ? std::optional<double>(best->accuracy) : std::nullopt,
                index < library.bpms.size() && library.bpms[index].main > 0 ? std::optional<double>(library.bpms[index].main * pitch) : std::nullopt,
                Favorites::contains(library.hashes[index]),
            };

            if (!matches_search(query, subject)) {
                continue;
            }

            visible.push_back(index);
        }

        sort_visible();
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
        refresh_search();
    }

    void SceneList::open_sort_menu() {
        overlays.open(std::make_unique<SortMenu>(skin, current_sort(), [this](ListSort sort) {
            set_sort(sort);
        }));
    }

    void SceneList::sort_visible() {
        ListSort sort = current_sort();

        if (sort == ListSort::TITLE) {
            return;
        }

        std::vector<std::string> artists;

        if (sort == ListSort::ARTIST) {
            artists.resize(library.size());

            for (size_t index : visible) {
                artists[index] = ascii_lowercase(library.charts[index]->info.artist);
            }
        }

        auto bpm = [&](size_t index) {
            return index < library.bpms.size() ? library.bpms[index].main : 0.0;
        };

        auto added = [&](size_t index) {
            return index < library.added.size() ? library.added[index] : (int64_t)0;
        };

        std::stable_sort(visible.begin(), visible.end(), [&](size_t a, size_t b) {
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

        apply_search(selected_library_index());

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

        if (search.empty()) {
            draw_text(font, "Type to search", {padding, baseline}, skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        std::string count = std::format("{} / {}", visible.size(), library.size());
        int32_t count_width = text_bounds(font, count).size.x;
        draw_text(font, search, {padding, baseline}, skin->color(SkinColor::TEXT));
        float hint_gap = Ui::layout_pxf("song_list.hint_gap");
        float clear_x = (float)padding + text_bounds(font, search).size.x + hint_gap;
        draw_hints(window, font, {{"Esc  clear", SPOT_SEARCH_CLEAR}}, {clear_x, (float)baseline}, hint_gap, skin->color(SkinColor::TEXT_FAINT), skin->color(SkinColor::TEXT), ui_spots);
        draw_text(font, count, {screensize.x - padding - GameNascent::fps_counter_width() - count_width, baseline}, skin->color(visible.empty() ? SkinColor::TEXT_ERROR : SkinColor::TEXT_MUTED));
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

    size_t SceneList::pack_chart_count() const {
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
        overlays.open(std::make_unique<ChartMenu>(skin, *selected_chart, pack_chart_count(), Favorites::contains(selected_hash()), [this](ChartAction action) {
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
            default:
                delete_charts(action == ChartAction::DELETE_PACK);
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

    void SceneList::toggle_favorite() {
        bool added = Favorites::toggle(selected_hash());
        game->notify(added ? std::format("Added {} to favorites", selected_chart->info.title) : std::format("Removed {} from favorites", selected_chart->info.title));
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

    void SceneList::delete_charts(bool whole_pack) {
        std::error_code ec;
        std::filesystem::path chart_path = selected_chart->file_path;

        if (ChartFolders::in_linked_folder(chart_path)) {
            game->notify("That chart is in a linked folder, so Nascent won't delete it", true);
            return;
        }

        std::filesystem::path folder = std::filesystem::weakly_canonical(chart_path.parent_path(), ec);
        std::filesystem::path songs = std::filesystem::weakly_canonical(SettingsManager::song_directory(), ec);

        if (ec || folder.empty() || folder.parent_path() != songs) {
            game->notify("That chart isn't in your songs folder, so it wasn't deleted", true);
            return;
        }

        std::vector<bool> removed(library.size(), false);
        size_t removed_count = 0;

        for (size_t index = 0; index < library.size(); index++) {
            bool in_pack = std::filesystem::weakly_canonical(library.charts[index]->file_path.parent_path(), ec) == folder;
            removed[index] = whole_pack ? in_pack : library.charts[index].get() == selected_chart;
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
                std::string extension = entry.path().extension().string();
                charts_left = charts_left || std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), extension) != CHART_FILE_EXTENSIONS.end();
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

        ChartLibrary remaining = library.without(removed);
        selected_chart = next;
        apply_library(std::move(remaining));

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
        float track_x = (float)side_panel_position(screensize).x - Ui::layout_pxf("song_list.scrollbar_gap") - track_width;
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
        }
        else {
            field->draw(window);
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

            float difficulty = (float)difficulty_cache->difficulty(*job.chart, job.pitch);

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
        std::unique_lock<std::mutex> lock(difficulty_mutex);
        difficulty_generation++;
        difficulty_jobs.clear();
        difficulty_results.clear();

        if (wait) {
            difficulty_cv.wait(lock, [this] { return !difficulty_busy; });
        }
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

        for (const DifficultyResult& result : results) {
            if (result.generation == difficulty_generation && result.index < library.difficulties.size()) {
                library.difficulties[result.index] = result.difficulty;
            }
        }
    }

    void SceneList::set_pitch(float pitch) {
        this->pitch = snap_pitch(pitch);
        status_bar.set_rate(this->pitch);
        cancel_difficulties(false);
        library.difficulties.assign(library.size(), std::nullopt);
        refresh_search();

        if (!visible.empty()) {
            select_chart(selected_chart_index);
        }
    }
}
