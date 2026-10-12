#include "Const.h"
#include "Account.h"
#include "SceneList.h"
#include "BeatmapDownloads.h"
#include "Chart.h"
#include "DifficultyCalculator.h"
#include "EntityField.h"
#include "Game.h"
#include "InputManager.h"
#include "GradeColor.h"
#include "KeyEvents.h"
#include "LocalBests.h"
#include "Replay.h"
#include "Platform.h"
#include "ProgressBar.h"
#include "Scene.h"
#include "ServerApi.h"
#include "Skin.h"
#include "Util.h"
#include "SceneAccount.h"
#include "SceneBrowser.h"
#include "SceneCalibration.h"
#include "SceneInput.h"
#include "SceneOptions.h"
#include "ScenePlay.h"
#include "SettingsManager.h"
#include "Sha256.h"
#include "TextCache.h"
#include "Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <thread>
#include <iterator>
#include <locale>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace nascent {
    static constexpr float DIFFICULTY_BAR_MAX = 100.0f;
    static constexpr float DIFFICULTY_BAR_WIDTH = 300.0f;
    static constexpr float DIFFICULTY_BAR_HEIGHT = 6.0f;
    static constexpr float LENGTH_OFFSET = 90.0f;
    static constexpr float GRADE_SCREEN_MARGIN = 8.0f;
    static constexpr float GRADE_GAP = 8.0f;
    static constexpr double SEARCH_TOLERANCE = 0.5;
    static constexpr float LEADERBOARD_DELAY = 0.25f;
    static constexpr int32_t ONLINE_LEADERBOARD_LIMIT = 50;
    static constexpr int32_t LOCAL_LEADERBOARD_ROWS = 8;
    static constexpr int32_t LEADERBOARD_PADDING = 24;
    static constexpr int32_t LEADERBOARD_ROW_HEIGHT = 30;
    static constexpr int32_t LEADERBOARD_HEADER_HEIGHT = 52;
    static constexpr int32_t LEADERBOARD_SECTION_GAP = 30;
    static constexpr int32_t LEADERBOARD_GRADE_OFFSET = 56;
    static constexpr int32_t LEADERBOARD_ACCURACY_OFFSET = 90;
    static constexpr int32_t LEADERBOARD_NAME_OFFSET = 190;
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

    static constexpr int32_t RECOMMENDED_ROW_HEIGHT = 30;
    static constexpr int32_t RECOMMENDED_STATUS_OFFSET = 540;
    static const olc::Pixel LEADERBOARD_BACKGROUND = olc::Pixel(0, 0, 0, 150);
    static const olc::Pixel LEADERBOARD_HIGHLIGHT = olc::Pixel(255, 255, 255, 24);

    enum class ChartAction : uint8_t {
        OPEN_WEBSITE,
        DELETE_CHART,
        DELETE_PACK
    };

    static constexpr std::array<ChartAction, 3> CHART_ACTIONS = {ChartAction::OPEN_WEBSITE, ChartAction::DELETE_CHART, ChartAction::DELETE_PACK};
    static constexpr int32_t CHART_MENU_MIN_WIDTH = 720;
    static constexpr int32_t CHART_MENU_SCREEN_MARGIN = 120;
    static constexpr int32_t CHART_MENU_PADDING = 32;
    static constexpr int32_t CHART_MENU_ROW_HEIGHT = 44;
    static constexpr int32_t CHART_MENU_ROW_INSET = 14;
    static constexpr int32_t CHART_MENU_GAP = 14;
    static constexpr uint8_t CHART_MENU_DIM = 160;
    static const olc::Pixel CHART_MENU_BACKGROUND = olc::Pixel(10, 10, 14, 235);
    static const olc::Pixel CHART_MENU_DANGER = olc::Pixel(235, 80, 80);

    template <typename F>
    static void parallel_for(size_t count, F work) {
        size_t thread_count = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), std::max<size_t>(count, 1));
        std::atomic<size_t> next = 0;
        std::vector<std::thread> threads;

        for (size_t t = 0; t < thread_count; t++) {
            threads.emplace_back([&] {
                for (size_t index = next++; index < count; index = next++) {
                    work(index);
                }
            });
        }

        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    struct LoadedChart {
        std::unique_ptr<Chart> chart;
        std::u8string folder;
        float difficulty;
        std::string hash;
    };

    static std::u8string folder_key(const std::filesystem::path& folder_path) {
        std::u8string folder = folder_path.filename().u8string();

        for (char8_t& c : folder) {
            if (c >= u8'A' && c <= u8'Z') {
                c = (char8_t)(c - u8'A' + u8'a');
            }
        }

        return folder;
    }

    static ChartLibrary build_library(std::vector<LoadedChart> loaded, float pitch) {
        parallel_for(loaded.size(), [&](size_t index) {
            loaded[index].difficulty = (float)DifficultyCalculator::calculate(*loaded[index].chart);
        });

        std::unordered_map<std::u8string, std::string> folder_titles;

        for (const LoadedChart& entry : loaded) {
            std::string title = entry.chart->info.title;

            for (char& c : title) {
                c = (char)std::tolower((unsigned char)c);
            }

            folder_titles.emplace(entry.folder, std::move(title));
        }

        std::stable_sort(loaded.begin(), loaded.end(), [&](const LoadedChart& a, const LoadedChart& b) {
            const std::string& a_title = folder_titles.at(a.folder);
            const std::string& b_title = folder_titles.at(b.folder);

            if (a_title != b_title) {
                return a_title < b_title;
            }

            if (a.folder != b.folder) {
                return a.folder < b.folder;
            }

            if (a.difficulty != b.difficulty) {
                return a.difficulty < b.difficulty;
            }

            return a.chart->info.difficulty_name < b.chart->info.difficulty_name;
        });

        ChartLibrary library;
        library.pitch = pitch;

        for (LoadedChart& entry : loaded) {
            const ChartInfo& info = entry.chart->info;
            std::string text = std::format("{} {} {} {} {} {}", info.title, info.title_unicode, info.artist, info.artist_unicode, info.mapper, info.difficulty_name);

            for (char& c : text) {
                c = (char)std::tolower((unsigned char)c);
            }

            library.charts.push_back(std::move(entry.chart));
            library.hashes.push_back(std::move(entry.hash));
            library.difficulties.push_back(entry.difficulty);
            library.search_texts.push_back(std::move(text));
            library.details.push_back(std::format("[{}] - {} // {}", info.difficulty_name, info.artist, info.mapper));
            library.lengths_ms.push_back((double)library.charts.back()->get_duration_ms() * library.charts.back()->pitch);
        }

        return library;
    }

    static ChartLibrary load_library(const std::filesystem::path& song_directory, float pitch, bool hash_files) {
        std::error_code ec;
        std::vector<LoadedChart> loaded;

        if (std::filesystem::is_directory(song_directory, ec)) {
            struct ChartFile {
                std::filesystem::path path;
                std::u8string folder;
            };

            std::vector<ChartFile> chart_files;

            auto extension_rank = [](const std::filesystem::path& path) {
                auto found = std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), path.extension().string());
                return (size_t)(found - CHART_FILE_EXTENSIONS.begin());
            };

            for (const auto& chart_folder : std::filesystem::directory_iterator(song_directory, ec)) {
                if (!chart_folder.is_directory(ec)) {
                    continue;
                }

                std::u8string folder = folder_key(chart_folder.path());

                std::vector<std::filesystem::path> paths;

                for (const auto& chart_file : std::filesystem::directory_iterator(chart_folder.path(), ec)) {
                    if (extension_rank(chart_file.path()) < CHART_FILE_EXTENSIONS.size()) {
                        paths.push_back(chart_file.path());
                    }
                }

                std::sort(paths.begin(), paths.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                    return std::make_pair(extension_rank(a), a) < std::make_pair(extension_rank(b), b);
                });

                for (std::filesystem::path& path : paths) {
                    chart_files.push_back({std::move(path), folder});
                }
            }

            std::vector<LoadedChart> parsed(chart_files.size());
            std::mutex log_mutex;

            parallel_for(chart_files.size(), [&](size_t index) {
                std::string error;
                std::unique_ptr<Chart> chart = Chart::load(chart_files[index].path, pitch, error);

                std::error_code audio_ec;

                if (chart != nullptr && !std::filesystem::is_regular_file(chart->audio_path, audio_ec)) {
                    error = std::format("audio file {} is missing", path_utf8(chart->audio_path));
                    chart.reset();
                }

                if (chart == nullptr) {
                    std::lock_guard<std::mutex> lock(log_mutex);
                    std::cerr << "Skipping chart " << path_utf8(chart_files[index].path) << ": " << error << std::endl;
                    return;
                }

                std::string hash = chart->gameplay_hash();
                parsed[index] = {std::move(chart), chart_files[index].folder, 0.0f, std::move(hash)};
            });

            std::unordered_set<std::string> loaded_hashes;

            for (LoadedChart& entry : parsed) {
                if (entry.chart != nullptr && loaded_hashes.insert(entry.hash).second) {
                    loaded.push_back(std::move(entry));
                }
            }
        }

        ChartLibrary library = build_library(std::move(loaded), pitch);
        library.hashed_files = hash_files;

        if (hash_files) {
            std::vector<std::optional<std::string>> file_hashes(library.charts.size());

            parallel_for(library.charts.size(), [&](size_t index) {
                file_hashes[index] = sha256_file_hex(library.charts[index]->file_path);
            });

            for (size_t index = 0; index < file_hashes.size(); index++) {
                if (file_hashes[index]) {
                    library.hash_by_replay_folder[file_hashes[index]->substr(0, Replay::FOLDER_HASH_LENGTH)] = library.hashes[index];
                }
            }
        }

        return library;
    }

    static std::vector<LocalScore> load_local_scores(const std::filesystem::path& chart_path, const std::filesystem::path& replay_directory, long rate) {
        std::vector<LocalScore> scores;
        std::optional<std::string> file_hash = sha256_file_hex(chart_path);

        if (!file_hash) {
            return scores;
        }

        std::error_code ec;

        for (const auto& file : std::filesystem::directory_iterator(replay_directory / file_hash->substr(0, Replay::FOLDER_HASH_LENGTH), ec)) {
            std::string error;
            std::optional<Replay> replay = load_replay(file.path(), error);

            if (replay && std::lround(replay->rate * 100.0f) == rate) {
                scores.push_back({replay->result.accuracy, replay->result.max_combo, replay->played_at});
            }
        }

        std::sort(scores.begin(), scores.end(), [](const LocalScore& a, const LocalScore& b) {
            return a.accuracy != b.accuracy ? a.accuracy > b.accuracy : a.max_combo > b.max_combo;
        });

        return scores;
    }

    template <typename T>
    static bool request_ready(const std::optional<T>& request) {
        return request && request->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    SceneList::SceneList(Skin* skin, EntityStarfield* sky) {
        this->skin = skin;
        this->sky = sky;

        background_loader = std::thread([this] {
            background_load_loop();
        });

        difficulty_worker = std::thread([this] {
            difficulty_loop();
        });
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
    }

    void SceneList::init(Game*) {
        if (!pending_selection_path.empty()) {
            std::string error;
            std::unique_ptr<Chart> chart = Chart::load(pending_selection_path, pitch, error);

            if (chart != nullptr) {
                std::vector<LoadedChart> loaded;
                std::string hash = chart->gameplay_hash();
                loaded.push_back({std::move(chart), folder_key(pending_selection_path.parent_path()), 0.0f, std::move(hash)});
                apply_library(build_library(std::move(loaded), pitch));
            }
        }

        start_library_load();
    };

    void SceneList::on_enter() {
        if (BeatmapDownloads::take_library_changed()) {
            start_library_load();
        }

        refresh_search();

        if (pending_selection) {
            auto found = std::find(chart_hashes.begin(), chart_hashes.end(), *pending_selection);
            int audio_id = std::exchange(pending_audio_id, -1);
            pending_selection.reset();
            pending_selection_path.clear();

            if (found != chart_hashes.end()) {
                size_t chart = (size_t)(found - chart_hashes.begin());
                apply_search(chart);

                if (std::find(visible.begin(), visible.end(), chart) == visible.end()) {
                    search.clear();
                    apply_search(chart);
                }

                if (audio_id != -1) {
                    stop_preview_audio();
                    selected_chart_audio_id = audio_id;
                    preview_audio_chart = charts[chart].get();
                    audio_fadein_timer = 1.0f;
                    audio_id = -1;
                }
            }

            if (audio_id != -1) {
                game->get_audio().Stop(audio_id);
                game->get_audio().UnloadSound(audio_id);
            }
        }

        if (!visible.empty()) {
            select_chart(selected_chart_index);
        }

        timer = 0;
        status_bar.refresh();
        leaderboard_generation++;
        local_scores.clear();
        online_scores.clear();
    }

    void SceneList::on_exit() {
        stop_preview_audio();
        load_generation++;
    }

    void SceneList::update(float elapsed_time) {
        status_bar.update(elapsed_time);
        poll_library_load();
        collect_difficulties();
        update_leaderboards(elapsed_time);

        if (chart_menu) {
            update_chart_menu();
        }
        else if (!update_controls()) {
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
            apply_background_load(std::move(load_result->sprite));
        }

        crossfade_timer = std::max(0.0f, (crossfade_timer - elapsed_time * 6));

        double now = now_ms();

        if (selected_chart_audio_id != -1) {
            game->apply_music_volume(selected_chart_audio_id, audio_fadein_timer * audio_fadein_timer);
        }

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart && !game->get_audio().IsPlaying(selected_chart_audio_id)) {
            game->get_audio().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
            game->get_audio().Play(selected_chart_audio_id, true);
        }

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart) {
            preview_clock.sync(game->get_audio().GetPositionMilliseconds(selected_chart_audio_id) / selected_chart->pitch, now);
        }
        else {
            preview_clock.reset(selected_chart->info.preview_time / selected_chart->pitch, now);
        }

        preview_position = preview_clock.position_at(now);
        field->set_position(preview_position);
        field->opacity = selected_chart_audio_id != -1 && preview_audio_chart == selected_chart ? audio_fadein_timer : 0.0f;

        float fade_in_time = SettingsManager::settings.list_preview_fade_in_time;
        audio_fadein_timer = fade_in_time > 0 ? std::min(1.0f, audio_fadein_timer + elapsed_time / fade_in_time) : 1.0f;

        timer += elapsed_time;

        if (crossfade && crossfade_timer == 0.0f) {
            crossfade = false;
            old_background_image.reset();
        }

        list_scroll_offset *= std::abs(list_scroll_offset) > 1.0 ? (std::pow(0.005, elapsed_time * 2)) : 0;
    };

    bool SceneList::update_controls() {
        if (game->window->GetKey(InputManager::calibration_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneCalibration>(skin, this));
            return false;
        }

        if (game->window->GetKey(InputManager::account_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneAccount>(skin, this, sky));
            return false;
        }

        if (game->window->GetKey(InputManager::browse_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneBrowser>(skin, this));
            return false;
        }

        if (game->window->GetKey(InputManager::options_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneOptions>(skin));
            return false;
        }

        if (game->window->GetKey(InputManager::input_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneInput>(skin, this, selected_chart != nullptr ? selected_chart->info.key_count : (uint8_t)4));
            return false;
        }

        update_search();

        if (game->window->GetKey(InputManager::leaderboard_key).bPressed) {
            show_leaderboards = !show_leaderboards;
            status_bar.set_leaderboards_shown(show_leaderboards);
            leaderboard_delay = 0;
            game->play_sound(skin->sound(SkinSound::HITSOUND));
        }

        if (game->window->GetKey(InputManager::keybind_mod1).bHeld) {
            if (game->window->GetKey(InputManager::menu_left_key).bPressed) {
                set_pitch(pitch - PITCH_INTERVAL);
            }
            if (game->window->GetKey(InputManager::menu_right_key).bPressed) {
                set_pitch(pitch + PITCH_INTERVAL);
            }
        }

        if (charts.empty() && (!library_loading || recommended_started) && game->window->GetKey(InputManager::menu_start_key).bPressed) {
            download_recommended();
        }

        if (visible.empty()) {
            return false;
        }

        if (game->window->GetKey(InputManager::chart_menu_key).bPressed) {
            chart_menu = ChartMenu();
            game->play_sound(skin->sound(SkinSound::HITSOUND));
            return true;
        }

        if (game->window->GetKey(InputManager::random_chart_key).bPressed) {
            select_chart(rand() % visible.size());
        }

        if (game->window->GetKey(InputManager::menu_down_key).bPressed) {
            select_chart(positive_modulo((selected_chart_index + 1), visible.size()));
        }

        if (game->window->GetKey(InputManager::menu_up_key).bPressed) {
            select_chart(positive_modulo((selected_chart_index - 1), visible.size()));
        }

        int mwheel = game->window->GetMouseWheel();
        if (mwheel != 0) {
            select_chart(positive_modulo(
                selected_chart_index + ((mwheel < 0) - (mwheel > 0)),
                visible.size()
            ));
        }

        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            game->set_scene(std::make_unique<ScenePlay>(selected_chart, skin, this));
        }

        return true;
    }

    void SceneList::select_chart(int32_t index, bool sound) {
        selected_chart = charts[visible[index]].get();
        status_bar.set_key_count(selected_chart->info.key_count);
        leaderboard_delay = LEADERBOARD_DELAY;

        selected_chart->set_pitch(this->pitch);

        int32_t down_steps = (index - selected_chart_index + visible.size()) % visible.size();
        int32_t up_steps = (selected_chart_index - index + visible.size()) % visible.size();

        list_scroll_offset += std::clamp(
            (up_steps < down_steps ? -up_steps : down_steps) * list_chart_height,
            list_chart_height * -5,
            list_chart_height * 5
        );

        selected_chart_index = index;

        if (sound) {
            game->play_sound(skin->sound(SkinSound::HITSOUND));
        }

        olc::vi2d window_size = game->window->GetScreenSize();
        field = std::make_unique<EntityField>(selected_chart, skin, olc::vd2d{(double)window_size.x / 4 * 3 - (double)window_size.x / 48, 0.0}, olc::vd2d{(double)(window_size.x / 4), (double)window_size.y});
        field->draw_judge = false;
        field->upscroll = SettingsManager::settings.field_upscroll;

        double now = now_ms();

        preview_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;

        if (selected_chart_audio_id != -1 && preview_audio_chart != nullptr && preview_audio_chart->audio_path == selected_chart->audio_path) {
            preview_audio_chart = selected_chart;
            game->get_audio().SetPitch(selected_chart_audio_id, selected_chart->pitch);
            preview_clock.reset(game->get_audio().GetPositionMilliseconds(selected_chart_audio_id) / selected_chart->pitch, now);
        }
        else {
            preview_clock.reset(selected_chart->info.preview_time / selected_chart->pitch, now);
        }

        preview_position = preview_clock.position_at(now);
        field->set_position(preview_position);
        field->opacity = 0.0f;

        request_background_load();
    }

    void SceneList::request_background_load() {
        load_generation++;

        {
            std::lock_guard<std::mutex> lock(background_load_mutex);
            pending_background_load = BackgroundLoadRequest{
                load_generation,
                selected_chart->info.image_file.empty() ? "" : selected_chart->image_path.string()
            };
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

            std::unique_ptr<olc::Sprite> sprite;

            if (!request.image_path.empty()) {
                sprite = std::make_unique<olc::Sprite>();

                if (sprite->LoadFromFile(request.image_path) != olc::OK) {
                    sprite.reset();
                }
            }

            std::lock_guard<std::mutex> lock(background_load_mutex);
            completed_background_load = BackgroundLoadResult{request.generation, std::move(sprite)};
        }
    }

    void SceneList::apply_background_load(std::unique_ptr<olc::Sprite> sprite) {
        old_background_image = std::move(background_image);
        background_image = SpriteDecal(std::move(sprite), true);

        crossfade_timer = 1;
        crossfade = true;

        if (preview_audio_chart == selected_chart) {
            return;
        }

        stop_preview_audio();

        selected_chart_audio_id = load_song_audio(game->get_audio(), selected_chart->audio_path);
        preview_audio_chart = selected_chart;
        audio_fadein_timer = 0.0f;
        game->apply_music_volume(selected_chart_audio_id, 0.0f);
        game->get_audio().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
        game->get_audio().SetPitch(selected_chart_audio_id, selected_chart->pitch);
        game->get_audio().Play(selected_chart_audio_id, true);
    }

    void SceneList::stop_preview_audio() {
        if (selected_chart_audio_id != -1) {
            game->get_audio().Stop(selected_chart_audio_id);
            game->get_audio().UnloadSound(selected_chart_audio_id);
            selected_chart_audio_id = -1;
            preview_audio_chart = nullptr;
        }
    }

    void SceneList::start_library_load() {
        if (library_loading) {
            library_reload_pending = true;
            return;
        }

        library_loading = true;
        std::filesystem::path song_directory = SettingsManager::song_directory();
        float load_pitch = pitch;
        bool hash_files = !LocalBests::is_backfilled();

        library_loader = std::thread([this, song_directory, load_pitch, hash_files] {
            ChartLibrary library = load_library(song_directory, load_pitch, hash_files);
            std::lock_guard<std::mutex> lock(library_load_mutex);
            completed_library_load = std::move(library);
        });
    }

    void SceneList::poll_library_load() {
        if (BeatmapDownloads::take_library_changed()) {
            start_library_load();
        }

        if (!library_loading || game->is_transitioning()) {
            return;
        }

        std::optional<ChartLibrary> library;
        {
            std::lock_guard<std::mutex> lock(library_load_mutex);
            library = std::exchange(completed_library_load, std::nullopt);
        }

        if (!library) {
            return;
        }

        library_loader.join();
        library_loading = false;

        if (library->hashed_files && !LocalBests::is_backfilled()) {
            LocalBests::backfill(library->hash_by_replay_folder, user_config_directory(SettingsManager::APP_NAME) / "replays");
        }

        apply_library(std::move(*library));

        if (std::exchange(library_reload_pending, false)) {
            start_library_load();
        }
    }

    void SceneList::apply_library(ChartLibrary library) {
        std::string selected_hash = selected_chart != nullptr ? selected_chart->gameplay_hash() : std::string();
        std::string preview_hash = preview_audio_chart != nullptr ? preview_audio_chart->gameplay_hash() : std::string();

        cancel_difficulties(true);

        charts = std::move(library.charts);
        chart_hashes = std::move(library.hashes);
        search_texts = std::move(library.search_texts);
        chart_details = std::move(library.details);
        chart_lengths_ms = std::move(library.lengths_ms);
        chart_difficulties = std::move(library.difficulties);
        local_hashes = std::unordered_set<std::string>(chart_hashes.begin(), chart_hashes.end());

        if (library.pitch != pitch) {
            chart_difficulties.assign(charts.size(), std::nullopt);
        }

        auto find_chart = [&](const std::string& hash) {
            auto found = std::find(chart_hashes.begin(), chart_hashes.end(), hash);
            return !hash.empty() && found != chart_hashes.end() ? (size_t)(found - chart_hashes.begin()) : SIZE_MAX;
        };

        size_t selected = find_chart(selected_hash);
        size_t preview = find_chart(preview_hash);
        selected_chart = nullptr;
        preview_audio_chart = preview != SIZE_MAX ? charts[preview].get() : nullptr;

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

    struct SearchFilter {
        std::string field;
        std::string op;
        std::string value;
    };

    struct SearchQuery {
        std::vector<std::string> words;
        std::vector<SearchFilter> filters;
    };

    static std::string lowercase(std::string text) {
        for (char& c : text) {
            c = (char)std::tolower((unsigned char)c);
        }

        return text;
    }

    static std::optional<double> parse_number(const std::string& text) {
        if (text.empty()) {
            return std::nullopt;
        }

        size_t colon = text.find(':');

        if (colon != std::string::npos) {
            std::optional<double> minutes = parse_number(text.substr(0, colon));
            std::optional<double> seconds = parse_number(text.substr(colon + 1));
            return minutes && seconds ? std::optional<double>(*minutes * 60.0 + *seconds) : std::nullopt;
        }

        std::istringstream stream(text);
        stream.imbue(std::locale::classic());
        double value = 0;
        stream >> value;
        return stream.fail() || !stream.eof() ? std::nullopt : std::optional<double>(value);
    }

    static std::optional<bool> parse_bool(const std::string& text) {
        if (text == "true" || text == "yes" || text == "1") {
            return true;
        }

        if (text == "false" || text == "no" || text == "0") {
            return false;
        }

        return std::nullopt;
    }

    static std::optional<int32_t> parse_grade(const std::string& text) {
        for (Grade grade : {Grade::X, Grade::S, Grade::A, Grade::B, Grade::C, Grade::D, Grade::E}) {
            if (lowercase(std::string(grade_name(grade))) == text) {
                return -(int32_t)grade;
            }
        }

        return std::nullopt;
    }

    static bool compare(double actual, const std::string& op, double expected, double tolerance) {
        if (op == "=") {
            return std::abs(actual - expected) <= tolerance;
        }

        if (op == "!=") {
            return std::abs(actual - expected) > tolerance;
        }

        if (op == "<") {
            return actual < expected;
        }

        if (op == ">") {
            return actual > expected;
        }

        if (op == "<=") {
            return actual <= expected;
        }

        return actual >= expected;
    }

    static std::string canonical_field(const std::string& field) {
        static const std::pair<const char*, const char*> aliases[] = {
            {"diff", "difficulty"},
            {"len", "length"},
            {"key", "keys"},
            {"note", "notes"},
        };

        for (const auto& [alias, name] : aliases) {
            if (field == alias) {
                return name;
            }
        }

        return field;
    }

    static bool is_search_field(const std::string& field) {
        static const char* fields[] = {"keys", "difficulty", "length", "notes", "played", "grade", "artist", "title", "mapper"};
        return std::any_of(std::begin(fields), std::end(fields), [&](const char* name) { return field == name; });
    }

    static SearchQuery parse_search(const std::string& search) {
        SearchQuery query;
        std::string token;

        auto finish = [&]() {
            if (token.empty()) {
                return;
            }

            std::string lowered = lowercase(token);
            size_t op_start = lowered.find_first_of("<>=!");

            if (op_start != std::string::npos && op_start > 0) {
                size_t op_end = op_start + 1;

                if (op_end < lowered.size() && lowered[op_end] == '=' && lowered[op_start] != '=') {
                    op_end++;
                }

                std::string field = canonical_field(lowered.substr(0, op_start));
                std::string op = lowered.substr(op_start, op_end - op_start);

                if (is_search_field(field) && op != "!") {
                    query.filters.push_back({field, op, lowered.substr(op_end)});
                    token.clear();
                    return;
                }
            }

            query.words.push_back(std::move(lowered));
            token.clear();
        };

        for (char c : search) {
            if (c == ' ') {
                finish();
            }
            else {
                token.push_back(c);
            }
        }

        finish();
        return query;
    }

    bool SceneList::matches_filter(size_t index, const SearchFilter& filter) {
        const Chart& chart = *charts[index];

        if (filter.value.empty()) {
            return true;
        }

        if (filter.field == "artist" || filter.field == "title" || filter.field == "mapper") {
            std::string text = lowercase(filter.field == "artist" ? chart.info.artist + " " + chart.info.artist_unicode : filter.field == "title" ? chart.info.title + " " + chart.info.title_unicode
                                                                                                                                                  : chart.info.mapper);
            bool contains = text.find(filter.value) != std::string::npos;
            return filter.op == "!=" ? !contains : filter.op == "=" ? contains
                                                                    : true;
        }

        if (filter.field == "played" || filter.field == "grade") {
            const LocalBest* best = LocalBests::lookup(chart_hashes[index], pitch);

            if (filter.field == "played") {
                std::optional<bool> wanted = parse_bool(filter.value);

                if (!wanted || (filter.op != "=" && filter.op != "!=")) {
                    return true;
                }

                return ((best != nullptr) == *wanted) == (filter.op == "=");
            }

            std::optional<int32_t> wanted = parse_grade(filter.value);

            if (!wanted) {
                return true;
            }

            return best && compare(-(double)grade_for_accuracy(best->accuracy), filter.op, *wanted, 0.0);
        }

        std::optional<double> wanted = parse_number(filter.value);

        if (!wanted) {
            return true;
        }

        if (filter.field == "keys") {
            return compare(chart.info.key_count, filter.op, *wanted, 0.0);
        }

        if (filter.field == "notes") {
            return compare((double)chart.hits.size(), filter.op, *wanted, 0.0);
        }

        if (filter.field == "length") {
            return compare(chart_lengths_ms[index] / pitch / 1000.0, filter.op, *wanted, SEARCH_TOLERANCE);
        }

        std::optional<float> rating = get_difficulty(index);
        return rating && compare(*rating, filter.op, *wanted, SEARCH_TOLERANCE);
    }

    void SceneList::apply_search(size_t keep_chart) {
        SearchQuery query = parse_search(search);
        visible.clear();
        selected_chart_index = 0;

        bool filters_difficulty = std::any_of(query.filters.begin(), query.filters.end(), [](const SearchFilter& filter) {
            return filter.field == "difficulty";
        });

        if (filters_difficulty) {
            if (chart_difficulties.size() != charts.size()) {
                chart_difficulties.assign(charts.size(), std::nullopt);
            }

            parallel_for(charts.size(), [&](size_t index) {
                if (!chart_difficulties[index]) {
                    chart_difficulties[index] = (float)DifficultyCalculator::calculate_at_pitch(*charts[index], pitch);
                }
            });
        }

        for (size_t index = 0; index < charts.size(); index++) {
            bool matches = std::all_of(query.words.begin(), query.words.end(), [&](const std::string& term) {
                return search_texts[index].find(term) != std::string::npos;
            });

            matches = matches && std::all_of(query.filters.begin(), query.filters.end(), [&](const SearchFilter& filter) {
                          return matches_filter(index, filter);
                      });

            if (!matches) {
                continue;
            }

            if (index == keep_chart) {
                selected_chart_index = (int32_t)visible.size();
            }

            visible.push_back(index);
        }

        list_scroll_offset = 0;
    }

    void SceneList::refresh_search() {
        apply_search(selected_chart != nullptr && !visible.empty() ? visible[selected_chart_index] : SIZE_MAX);
    }

    void SceneList::update_search() {
        olc::PixelGameEngine* window = game->window;
        std::string before = search;
        search += game->get_frame_text();

        for (int32_t keycode : window->GetKeyPressCache()) {
            if (window->ConvertKeycode(keycode) == olc::Key::BACK && !search.empty()) {
                search.pop_back();

                while (!search.empty() && ((unsigned char)search.back() & 0xc0) == 0x80) {
                    search.pop_back();
                }
            }
        }

        if (window->GetKey(InputManager::back_key).bPressed) {
            search.clear();
        }

        if (search == before) {
            return;
        }

        size_t keep_chart = !visible.empty() ? visible[selected_chart_index] : SIZE_MAX;
        apply_search(keep_chart);

        if (!visible.empty() && charts[visible[selected_chart_index]].get() != selected_chart) {
            select_chart(selected_chart_index);
            list_scroll_offset = 0;
        }
    }

    void SceneList::draw_search(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();
        olc::Font& font = skin->display_font_small;
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = (StatusBar::HEIGHT - metrics.size.y) / 2 - metrics.offset.y;

        window->FillRectDecal({0.0f, 0.0f}, {(float)screensize.x, (float)StatusBar::HEIGHT}, StatusBar::BACKGROUND);
        window->FillRectDecal({0.0f, (float)(StatusBar::HEIGHT - 1)}, {(float)screensize.x, 1.0f}, StatusBar::EDGE);

        if (search.empty()) {
            draw_text(font, "Type to search", {StatusBar::PADDING, baseline}, olc::DARK_GREY);
            return;
        }

        std::string count = std::format("{} / {}", visible.size(), charts.size());
        int32_t count_width = text_bounds(font, count).size.x;
        draw_text(font, search, {StatusBar::PADDING, baseline}, olc::WHITE);
        draw_text(font, count, {screensize.x - StatusBar::PADDING - count_width, baseline}, visible.empty() ? olc::RED : olc::GREY);
    }

    std::string SceneList::leaderboard_key() const {
        return std::format("{}@{}", chart_hashes[visible[selected_chart_index]], std::lround(pitch * 100.0f));
    }

    void SceneList::update_leaderboards(float elapsed_time) {
        if (request_ready(local_request)) {
            std::vector<LocalScore> scores = local_request->result.get();

            if (local_request->generation == leaderboard_generation) {
                local_scores[local_request->key] = std::move(scores);
            }

            local_request.reset();
        }

        if (request_ready(online_request)) {
            ApiResult<ChartLeaderboard> board = online_request->result.get();

            if (online_request->generation == leaderboard_generation) {
                online_scores.insert_or_assign(online_request->key, std::move(board));
            }

            online_request.reset();
        }

        leaderboard_delay = std::max(0.0f, leaderboard_delay - elapsed_time);

        if (!show_leaderboards || selected_chart == nullptr || visible.empty() || leaderboard_delay > 0) {
            return;
        }

        std::string key = leaderboard_key();

        if (!local_request && !local_scores.contains(key)) {
            std::filesystem::path chart_path = selected_chart->file_path;
            std::filesystem::path replay_directory = user_config_directory(SettingsManager::APP_NAME) / "replays";
            long rate = std::lround(pitch * 100.0f);

            local_request = LeaderboardRequest<std::vector<LocalScore>>{key, leaderboard_generation, std::async(std::launch::async, [chart_path, replay_directory, rate] {
                                                                            return load_local_scores(chart_path, replay_directory, rate);
                                                                        })};
        }

        if (!online_request && !online_scores.contains(key)) {
            online_request = LeaderboardRequest<ApiResult<ChartLeaderboard>>{key, leaderboard_generation, ServerApi::chart_leaderboard(chart_hashes[visible[selected_chart_index]], pitch, ONLINE_LEADERBOARD_LIMIT)};
        }
    }

    void SceneList::draw_leaderboards(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t left = screen.x / 4 * 3 - screen.x / 48;
        int32_t width = screen.x / 4;
        int32_t top = StatusBar::HEIGHT;
        int32_t bottom = screen.y - StatusBar::HEIGHT;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)(bottom - top)}, LEADERBOARD_BACKGROUND);

        olc::Font& header_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect header_metrics = text_bounds(header_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t x = left + LEADERBOARD_PADDING;
        int32_t right = left + width - LEADERBOARD_PADDING;
        int32_t y = top + LEADERBOARD_PADDING;
        std::string key = leaderboard_key();
        std::string own_name = Account::signed_in() ? Account::username() : std::string();

        auto header = [&](std::string_view title, std::string_view detail) {
            draw_text(header_font, title, {x, y - header_metrics.offset.y}, olc::WHITE);

            if (!detail.empty()) {
                int32_t detail_width = text_bounds(row_font, detail).size.x;
                draw_text(row_font, detail, {right - detail_width, y - header_metrics.offset.y}, olc::GREY);
            }

            y += LEADERBOARD_HEADER_HEIGHT;
        };

        auto message = [&](std::string_view text, olc::Pixel color) {
            draw_text(row_font, text, {x, y - row_metrics.offset.y}, color);
            y += LEADERBOARD_ROW_HEIGHT;
        };

        auto row = [&](int64_t rank, double accuracy, std::string name, uint32_t combo, bool highlight) {
            int32_t baseline = y + (LEADERBOARD_ROW_HEIGHT - row_metrics.size.y) / 2 - row_metrics.offset.y;
            Grade grade = grade_for_accuracy(accuracy);
            std::string combo_text = std::format("{}x", combo);
            int32_t combo_width = text_bounds(row_font, combo_text).size.x;
            int32_t name_width = right - combo_width - LEADERBOARD_PADDING / 2 - (x + LEADERBOARD_NAME_OFFSET);

            if (highlight) {
                window->FillRectDecal({(float)(left + LEADERBOARD_PADDING / 2), (float)y}, {(float)(width - LEADERBOARD_PADDING), (float)LEADERBOARD_ROW_HEIGHT}, LEADERBOARD_HIGHLIGHT);
            }

            while (!name.empty() && text_bounds(row_font, name).size.x > name_width) {
                name.pop_back();

                while (!name.empty() && ((unsigned char)name.back() & 0xc0) == 0x80) {
                    name.pop_back();
                }

                if (!name.empty() && (unsigned char)name.back() >= 0xc0) {
                    name.pop_back();
                }
            }

            draw_text(row_font, std::format("#{}", rank), {x, baseline}, olc::GREY);
            draw_text(row_font, grade_name(grade), {x + LEADERBOARD_GRADE_OFFSET, baseline}, grade_color(grade));
            draw_text(row_font, std::format("{:.2f}%", std::floor(accuracy * 10000.0) / 100.0), {x + LEADERBOARD_ACCURACY_OFFSET, baseline}, olc::WHITE);
            draw_text(row_font, name, {x + LEADERBOARD_NAME_OFFSET, baseline}, highlight ? olc::WHITE : olc::GREY);
            draw_text(row_font, combo_text, {right - combo_width, baseline}, olc::GREY);
            y += LEADERBOARD_ROW_HEIGHT;
        };

        auto local = local_scores.find(key);
        header("Local", local != local_scores.end() && !local->second.empty() ? std::format("{} plays", local->second.size()) : std::string());

        if (local == local_scores.end()) {
            message("Loading...", olc::GREY);
        }
        else if (local->second.empty()) {
            message("No plays at this rate yet", olc::DARK_GREY);
        }
        else {
            for (size_t index = 0; index < local->second.size() && (int32_t)index < LOCAL_LEADERBOARD_ROWS; index++) {
                const LocalScore& score = local->second[index];
                row((int64_t)index + 1, score.accuracy, score.played_at.substr(0, 10), score.max_combo, false);
            }
        }

        y = std::max(y, top + LEADERBOARD_PADDING + LEADERBOARD_HEADER_HEIGHT + LEADERBOARD_ROW_HEIGHT * LOCAL_LEADERBOARD_ROWS) + LEADERBOARD_SECTION_GAP;

        auto online = online_scores.find(key);
        const ChartLeaderboard* board = online != online_scores.end() && online->second.ok() ? &*online->second.value : nullptr;
        header("Online", board != nullptr && board->total > 0 ? std::format("{} players", board->total) : std::string());

        if (online == online_scores.end()) {
            message("Loading...", olc::GREY);
            return;
        }

        if (board == nullptr) {
            message(online->second.error(), olc::DARK_GREY);
            return;
        }

        if (board->entries.empty()) {
            message("No plays at this rate yet", olc::DARK_GREY);
            return;
        }

        bool viewer_listed = false;
        int32_t rows = (bottom - LEADERBOARD_PADDING - y) / LEADERBOARD_ROW_HEIGHT;

        for (const RankedPlay& entry : board->entries) {
            bool own = entry.play.user && !own_name.empty() && entry.play.user->username == own_name;
            bool reserve = board->viewer && !viewer_listed && !own;

            if (rows - (reserve ? 1 : 0) <= 0) {
                break;
            }

            viewer_listed = viewer_listed || own;
            row(entry.rank, entry.play.accuracy, entry.play.user ? entry.play.user->username : std::string(), (uint32_t)entry.play.max_combo, own);
            rows--;
        }

        if (board->viewer && !viewer_listed && rows > 0) {
            row(board->viewer->rank, board->viewer->play.accuracy, own_name, (uint32_t)board->viewer->play.max_combo, true);
        }
    }

    void SceneList::download_recommended() {
        for (const RecommendedSong& song : RECOMMENDED_SONGS) {
            BeatmapDownloads::start(song.set_id, song.artist, song.title);
        }

        recommended_started = true;
        game->play_sound(skin->get_random_menu_pluck());
    }

    void SceneList::draw_recommended(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;
        bool any_failed = false;
        bool all_started = true;

        draw_text(skin->display_font_large, "Your library is empty", {x, y}, olc::WHITE);
        y += 70;
        draw_text(skin->display_font_small, "Download these recommended songs to get started.", {x, y}, olc::GREY);
        y += 60;

        for (const RecommendedSong& song : RECOMMENDED_SONGS) {
            std::shared_ptr<BeatmapDownloads::Download> download = BeatmapDownloads::find(song.set_id);
            std::string status;
            olc::Pixel status_color = olc::GREY;

            if (download == nullptr) {
                all_started = false;
            }
            else {
                switch (download->state.load()) {
                    case BeatmapDownloads::State::DOWNLOADING:
                        status = std::format("{:.0f}%", download->progress.load() * 100.0);
                        break;
                    case BeatmapDownloads::State::EXTRACTING:
                        status = "Extracting";
                        break;
                    case BeatmapDownloads::State::DONE:
                        status = "Downloaded";
                        status_color = olc::Pixel(90, 220, 120);
                        break;
                    case BeatmapDownloads::State::FAILED:
                        status = "Failed";
                        status_color = olc::RED;
                        any_failed = true;
                        break;
                }
            }

            draw_text(skin->display_font_small, std::format("{} - {}", song.artist, song.title), {x, y}, olc::WHITE);
            draw_text(skin->display_font_small, status, {x + RECOMMENDED_STATUS_OFFSET, y}, status_color);
            y += RECOMMENDED_ROW_HEIGHT;
        }

        y += 30;

        std::string hint = !all_started ? "Enter:  download recommended songs        F5:  browse charts"
            : any_failed                ? "Enter:  retry failed downloads        F5:  browse charts"
                                        : "Downloading...        F5:  browse charts";
        draw_text(skin->display_font_small, hint, {x, y}, olc::GREY);
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
        draw_list(window);
        draw_search(window);
        status_bar.draw(window, skin);

        if (chart_menu) {
            draw_chart_menu(window);
        }
    }

    size_t SceneList::pack_chart_count() const {
        if (selected_chart == nullptr) {
            return 0;
        }

        std::filesystem::path folder = selected_chart->file_path.parent_path();
        return (size_t)std::count_if(charts.begin(), charts.end(), [&](const std::unique_ptr<Chart>& chart) {
            return chart->file_path.parent_path() == folder;
        });
    }

    void SceneList::update_chart_menu() {
        olc::PixelGameEngine* window = game->window;

        if (selected_chart == nullptr || visible.empty() || window->GetKey(InputManager::back_key).bPressed || window->GetKey(InputManager::chart_menu_key).bPressed) {
            chart_menu.reset();
            game->play_sound(skin->sound(SkinSound::HITSOUND));
            return;
        }

        ChartMenu& menu = *chart_menu;

        if (window->GetKey(InputManager::menu_down_key).bPressed || window->GetKey(InputManager::menu_up_key).bPressed) {
            bool down = window->GetKey(InputManager::menu_down_key).bPressed;
            menu.selected = (menu.selected + (down ? 1 : CHART_ACTIONS.size() - 1)) % CHART_ACTIONS.size();
            menu.confirming = false;
            game->play_sound(skin->sound(SkinSound::HITSOUND));
            return;
        }

        if (!window->GetKey(InputManager::menu_start_key).bPressed) {
            return;
        }

        ChartAction action = CHART_ACTIONS[menu.selected];

        if (action == ChartAction::OPEN_WEBSITE) {
            chart_menu.reset();
            game->play_sound(skin->get_random_menu_pluck());
            open_chart_website();
            return;
        }

        if (!menu.confirming) {
            menu.confirming = true;
            game->play_sound(skin->sound(SkinSound::HITSOUND));
            return;
        }

        chart_menu.reset();
        game->play_sound(skin->get_random_menu_pluck());
        delete_charts(action == ChartAction::DELETE_PACK);
    }

    void SceneList::open_chart_website() {
        if (!open_url(ServerApi::chart_page_url(selected_chart->gameplay_hash(), pitch))) {
            game->notify("Could not open the browser", true);
        }
    }

    void SceneList::delete_charts(bool whole_pack) {
        std::error_code ec;
        std::filesystem::path chart_path = selected_chart->file_path;
        std::filesystem::path folder = std::filesystem::weakly_canonical(chart_path.parent_path(), ec);
        std::filesystem::path songs = std::filesystem::weakly_canonical(SettingsManager::song_directory(), ec);

        if (ec || folder.empty() || folder.parent_path() != songs) {
            game->notify("That chart isn't in your songs folder, so it wasn't deleted", true);
            return;
        }

        std::vector<bool> removed(charts.size(), false);
        size_t removed_count = 0;

        for (size_t index = 0; index < charts.size(); index++) {
            bool in_pack = std::filesystem::weakly_canonical(charts[index]->file_path.parent_path(), ec) == folder;
            removed[index] = whole_pack ? in_pack : charts[index].get() == selected_chart;
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
                next = charts[candidate].get();
            }
        }

        ChartLibrary library;
        library.pitch = pitch;

        for (size_t index = 0; index < charts.size(); index++) {
            if (removed[index]) {
                continue;
            }

            library.charts.push_back(std::move(charts[index]));
            library.hashes.push_back(chart_hashes[index]);
            library.difficulties.push_back(index < chart_difficulties.size() ? chart_difficulties[index] : std::nullopt);
            library.search_texts.push_back(search_texts[index]);
            library.details.push_back(chart_details[index]);
            library.lengths_ms.push_back(chart_lengths_ms[index]);
        }

        selected_chart = next;
        apply_library(std::move(library));

        if (library_loading) {
            library_reload_pending = true;
        }

        game->notify(whole_pack ? std::format("Deleted {} ({} charts)", name, removed_count) : std::format("Deleted {}", name));
    }

    void SceneList::draw_chart_menu(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        olc::Font& title_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");

        std::string title = std::format("{} - {}", selected_chart->info.artist, selected_chart->info.title);
        std::string subtitle = std::format("[{}]  in  {}", selected_chart->info.difficulty_name, path_utf8(selected_chart->file_path.parent_path().filename()));
        size_t pack_count = pack_chart_count();
        std::string pack_charts = pack_count == 1 ? std::string("1 chart") : std::format("{} charts", pack_count);
        const ChartMenu& menu = *chart_menu;

        auto label = [&](ChartAction action, bool confirming) -> std::string {
            switch (action) {
                case ChartAction::OPEN_WEBSITE:
                    return "Open on website";
                case ChartAction::DELETE_CHART:
                    return confirming ? "Press Enter again to delete this chart" : "Delete chart";
                default:
                    return confirming ? std::format("Press Enter again to delete the pack and its {}", pack_charts) : std::format("Delete pack ({})", pack_charts);
            }
        };

        int32_t content_width = std::max(text_bounds(title_font, title).size.x, text_bounds(row_font, subtitle).size.x);

        for (size_t index = 0; index < CHART_ACTIONS.size(); index++) {
            content_width = std::max(content_width, text_bounds(row_font, label(CHART_ACTIONS[index], true)).size.x + CHART_MENU_ROW_INSET * 2);
        }

        int32_t width = std::clamp(content_width + CHART_MENU_PADDING * 2, CHART_MENU_MIN_WIDTH, screen.x - CHART_MENU_SCREEN_MARGIN * 2);
        int32_t height = CHART_MENU_PADDING * 2 + title_metrics.size.y + CHART_MENU_GAP + row_metrics.size.y + CHART_MENU_GAP * 2 + (int32_t)CHART_ACTIONS.size() * CHART_MENU_ROW_HEIGHT;
        int32_t left = (screen.x - width) / 2;
        int32_t top = (screen.y - height) / 2;
        int32_t x = left + CHART_MENU_PADDING;
        int32_t y = top + CHART_MENU_PADDING;

        window->FillRectDecal({0.0f, 0.0f}, {(float)screen.x, (float)screen.y}, olc::Pixel(0, 0, 0, CHART_MENU_DIM));
        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)height}, CHART_MENU_BACKGROUND);

        draw_text(title_font, title, {x, y - title_metrics.offset.y}, olc::WHITE);
        y += title_metrics.size.y + CHART_MENU_GAP;
        draw_text(row_font, subtitle, {x, y - row_metrics.offset.y}, olc::GREY);
        y += row_metrics.size.y + CHART_MENU_GAP * 2;

        for (size_t index = 0; index < CHART_ACTIONS.size(); index++) {
            ChartAction action = CHART_ACTIONS[index];
            bool selected = index == menu.selected;
            bool confirming = selected && menu.confirming;
            bool danger = action != ChartAction::OPEN_WEBSITE;

            if (selected) {
                window->FillRectDecal({(float)x, (float)y}, {(float)(width - CHART_MENU_PADDING * 2), (float)CHART_MENU_ROW_HEIGHT}, LEADERBOARD_HIGHLIGHT);
            }

            olc::Pixel color = confirming ? CHART_MENU_DANGER : selected ? olc::WHITE
                : danger                                                 ? olc::Pixel(200, 140, 140)
                                                                         : olc::GREY;
            int32_t baseline = y + (CHART_MENU_ROW_HEIGHT - row_metrics.size.y) / 2 - row_metrics.offset.y;
            draw_text(row_font, label(action, confirming), {x + CHART_MENU_ROW_INSET, baseline}, color);
            y += CHART_MENU_ROW_HEIGHT;
        }
    }

    void SceneList::draw_list(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        if (visible.empty()) {
            if (charts.empty() && (!library_loading || recommended_started)) {
                draw_recommended(window);
                return;
            }

            draw_text(skin->display_font_medium, charts.empty() ? std::string("Loading charts...") : std::string("No charts match your search"), {screensize.x / 48, screensize.y / 2}, olc::GREY);
            return;
        }

        float beat_progress = selected_chart->beat_progress_at(preview_position);

        uint8_t background_brightening = 15 * std::clamp((1.0 - beat_progress * 3 + std::max(0.0, beat_progress - 0.75) * 4), 0.0, 1.0);

        olc::vf2d rect[4] = {
            {0.0f, 0.0f},
            {0.0f, screensize.y - 1.0f},
            {screensize.x - 1.0f, screensize.y - 1.0f},
            {screensize.x - 1.0f, 0.0f}
        };

        if (background_image) {
            window->DrawWarpedDecal(
                background_image.get(),
                rect,
                olc::Pixel(240 + background_brightening, 240 + background_brightening, 240 + background_brightening, 25 * (1 - crossfade_timer))
            );
        }

        if (old_background_image) {
            window->DrawWarpedDecal(
                old_background_image.get(),
                rect,
                olc::Pixel(240 + background_brightening, 240 + background_brightening, 240 + background_brightening, 25 * crossfade_timer)
            );
        }

        const uint8_t charts_per_screen = screensize.y / list_chart_height;
        float list_margin = (float)screensize.x / 48;
        if (widest_grade < 0) {
            widest_grade = 0;

            for (Grade grade : {Grade::X, Grade::S, Grade::A, Grade::B, Grade::C, Grade::D, Grade::E}) {
                widest_grade = std::max(widest_grade, (float)text_bounds(skin->display_font_large, grade_name(grade)).size.x);
            }
        }

        float grade_indent = std::max(0.0f, GRADE_SCREEN_MARGIN + widest_grade + GRADE_GAP - list_margin);
        const ProgressBar difficulty_bar({DIFFICULTY_BAR_WIDTH, DIFFICULTY_BAR_HEIGHT});
        wanted_difficulties.clear();

        if (show_leaderboards) {
            draw_leaderboards(window);
        }
        else {
            field->draw(window);
        }

        for (int32_t index = selected_chart_index - (charts_per_screen / 2 + 5); index < selected_chart_index + (charts_per_screen / 2 + 5); index++) {
            size_t chart_index = visible[positive_modulo(index, visible.size())];
            Chart* chart = charts[chart_index].get();

            float y = (float)screensize.y / 2 + (index - selected_chart_index) * list_chart_height + list_scroll_offset;

            if (y < -list_chart_height || y > screensize.y + list_chart_height) {
                continue;
            }

            float x = list_margin + grade_indent + std::pow(std::max(300.0 - std::abs((float)screensize.y / 2 - (y)), 0.0) / 50.0, 2);

            draw_text(skin->display_font_medium, chart->info.title, {(int)x, (int)y}, index == selected_chart_index ? olc::WHITE : olc::GREY);

            draw_text(skin->display_font_small, chart_details[chart_index], {(int)x, (int)y + 36}, index == selected_chart_index ? olc::WHITE : olc::GREY);

            std::optional<float> rating = get_difficulty(chart_index);

            if (!rating) {
                wanted_difficulties.push_back(chart_index);
            }

            float fill = rating ? std::clamp(*rating / DIFFICULTY_BAR_MAX, 0.0f, 1.0f) : 0.0f;
            olc::Pixel bar_color = skin->title_gradient_at(fill, 0.5);
            bar_color.a = index == selected_chart_index ? 255 : 140;
            difficulty_bar.draw(window, {x, y + 50}, fill, bar_color);

            if (rating) {
                draw_text(skin->display_font_small, std::format("{:.2f}", *rating), {(int)(x + DIFFICULTY_BAR_WIDTH + 12), (int)y + 58}, index == selected_chart_index ? olc::WHITE : olc::GREY);
            }

            int64_t length_ms = std::llround(chart_lengths_ms[chart_index] / pitch);
            draw_text(skin->display_font_small, std::format("{}:{:02}", length_ms / 60000, (length_ms / 1000) % 60), {(int)(x + DIFFICULTY_BAR_WIDTH + LENGTH_OFFSET), (int)y + 58}, index == selected_chart_index ? olc::WHITE : olc::GREY);

            const LocalBest* best = LocalBests::lookup(chart_hashes[chart_index], pitch);

            if (best != nullptr) {
                Grade grade = grade_for_accuracy(best->accuracy);
                std::string_view grade_text = grade_name(grade);
                olc::FontRect grade_bounds = text_bounds(skin->display_font_large, grade_text);
                float grade_center_y = y + 18;
                draw_text(skin->display_font_large, grade_text, {(int)(x - GRADE_GAP - grade_bounds.size.x - grade_bounds.offset.x), (int)(grade_center_y - grade_bounds.size.y / 2 - grade_bounds.offset.y)}, grade_color(grade));
            }
        }

        request_difficulties();
    }

    std::optional<float> SceneList::get_difficulty(size_t index) const {
        return index < chart_difficulties.size() ? chart_difficulties[index] : std::nullopt;
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

            float difficulty = (float)DifficultyCalculator::calculate_at_pitch(*job.chart, job.pitch);

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
                difficulty_jobs.push_back({difficulty_generation, index, charts[index].get(), pitch});
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

        if (chart_difficulties.size() != charts.size()) {
            chart_difficulties.assign(charts.size(), std::nullopt);
        }

        for (const DifficultyResult& result : results) {
            if (result.generation == difficulty_generation && result.index < chart_difficulties.size()) {
                chart_difficulties[result.index] = result.difficulty;
            }
        }
    }

    void SceneList::set_pitch(float pitch) {
        this->pitch = snap_pitch(pitch);
        status_bar.set_rate(this->pitch);
        cancel_difficulties(false);
        chart_difficulties.assign(charts.size(), std::nullopt);
        refresh_search();

        if (!visible.empty()) {
            select_chart(selected_chart_index);
        }
    }
}
