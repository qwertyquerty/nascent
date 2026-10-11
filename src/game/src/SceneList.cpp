#include "Const.h"
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
#include "Skin.h"
#include "Util.h"
#include "SceneAccount.h"
#include "SceneBrowser.h"
#include "SceneCalibration.h"
#include "ScenePlay.h"
#include "SettingsManager.h"
#include "Sha256.h"
#include "TextCache.h"

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
    static constexpr int32_t DIFFICULTY_CALCULATIONS_PER_FRAME = 4;
    static constexpr float DIFFICULTY_BAR_WIDTH = 300.0f;
    static constexpr float DIFFICULTY_BAR_HEIGHT = 6.0f;
    static constexpr float LENGTH_OFFSET = 90.0f;
    static constexpr float GRADE_SCREEN_MARGIN = 8.0f;
    static constexpr float GRADE_GAP = 8.0f;
    static constexpr double SEARCH_TOLERANCE = 0.5;

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
                    error = std::format("audio file {} is missing", chart->audio_path.string());
                    chart.reset();
                }

                if (chart == nullptr) {
                    std::lock_guard<std::mutex> lock(log_mutex);
                    std::cerr << "Skipping chart " << chart_files[index].path.string() << ": " << error << std::endl;
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

    SceneList::SceneList(Skin* skin) {
        this->skin = skin;

        background_loader = std::thread([this] {
            background_load_loop();
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
    }

    void SceneList::on_exit() {
        stop_preview_audio();
        load_generation++;
    }

    void SceneList::update(float elapsed_time) {
        status_bar.update(elapsed_time);
        poll_library_load();

        if (game->window->GetKey(InputManager::calibration_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneCalibration>(skin, this));
            return;
        }

        if (game->window->GetKey(InputManager::account_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneAccount>(skin, this));
            return;
        }

        if (game->window->GetKey(InputManager::browse_key).bPressed) {
            game->play_sound(skin->get_random_menu_pluck());
            game->set_scene(std::make_unique<SceneBrowser>(skin, this));
            return;
        }

        update_search();

        if (game->window->GetKey(InputManager::keybind_mod1).bHeld) {
            if (game->window->GetKey(InputManager::menu_left_key).bPressed) {
                set_pitch(pitch - PITCH_INTERVAL);
            }
            if (game->window->GetKey(InputManager::menu_right_key).bPressed) {
                set_pitch(pitch + PITCH_INTERVAL);
            }
        }

        if (visible.empty()) {
            return;
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
            game->get_audio().SetVolume(selected_chart_audio_id, audio_fadein_timer * audio_fadein_timer);
        }

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart && !game->get_audio().IsPlaying(selected_chart_audio_id)) {
            game->get_audio().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
            game->get_audio().Play(selected_chart_audio_id, true);
        }

        if (selected_chart_audio_id != -1 && preview_audio_chart == selected_chart) {
            preview_clock.sync(game->get_audio().GetCursorMilliseconds(selected_chart_audio_id) / selected_chart->pitch, now);
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

    void SceneList::select_chart(int32_t index, bool sound) {
        selected_chart = charts[visible[index]].get();
        status_bar.set_key_count(selected_chart->info.key_count);

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
            game->play_sound(skin->hitsound_sfx);
        }

        olc::vi2d window_size = game->window->GetScreenSize();
        field = std::make_unique<EntityField>(selected_chart, skin, olc::vd2d{(double)window_size.x / 4 * 3 - (double)window_size.x / 48, 0.0}, olc::vd2d{(double)(window_size.x / 4), (double)window_size.y});
        field->draw_judge = false;

        double now = now_ms();

        preview_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;

        if (selected_chart_audio_id != -1 && preview_audio_chart != nullptr && preview_audio_chart->audio_path == selected_chart->audio_path) {
            preview_audio_chart = selected_chart;
            game->get_audio().SetPitch(selected_chart_audio_id, selected_chart->pitch);
            preview_clock.reset(game->get_audio().GetCursorMilliseconds(selected_chart_audio_id) / selected_chart->pitch, now);
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

        selected_chart_audio_id = game->get_audio().LoadSound(selected_chart->audio_path.string());
        preview_audio_chart = selected_chart;
        audio_fadein_timer = 0.0f;
        game->get_audio().SetVolume(selected_chart_audio_id, 0.0f);
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
        std::filesystem::path song_directory = SettingsManager::settings.list_song_directory;
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

        charts = std::move(library.charts);
        chart_hashes = std::move(library.hashes);
        search_texts = std::move(library.search_texts);
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
            std::optional<LocalBest> best = LocalBests::lookup(chart_hashes[index], pitch);

            if (filter.field == "played") {
                std::optional<bool> wanted = parse_bool(filter.value);

                if (!wanted || (filter.op != "=" && filter.op != "!=")) {
                    return true;
                }

                return (best.has_value() == *wanted) == (filter.op == "=");
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
            return compare((double)chart.get_duration_ms() * chart.pitch / pitch / 1000.0, filter.op, *wanted, SEARCH_TOLERANCE);
        }

        int32_t budget = (int32_t)charts.size();
        std::optional<float> rating = get_difficulty(index, budget);
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
                    charts[index]->set_pitch(pitch);
                    chart_difficulties[index] = (float)DifficultyCalculator::calculate(*charts[index]);
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

        olc::Pixel::Mode mode = window->GetPixelMode();
        window->SetPixelMode(olc::Pixel::ALPHA);
        window->FillRect({0, 0}, {screensize.x, StatusBar::HEIGHT}, StatusBar::BACKGROUND);
        window->DrawLine({0, StatusBar::HEIGHT - 1}, {screensize.x, StatusBar::HEIGHT - 1}, StatusBar::EDGE);
        window->SetPixelMode(mode);

        if (search.empty()) {
            draw_text(font, "Type to search", {StatusBar::PADDING, baseline}, olc::DARK_GREY);
            return;
        }

        std::string count = std::format("{} / {}", visible.size(), charts.size());
        int32_t count_width = text_bounds(font, count).size.x;
        draw_text(font, search, {StatusBar::PADDING, baseline}, olc::WHITE);
        draw_text(font, count, {screensize.x - StatusBar::PADDING - count_width, baseline}, visible.empty() ? olc::RED : olc::GREY);
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
    }

    void SceneList::draw_list(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        if (visible.empty()) {
            draw_text(skin->display_font_medium, library_loading && charts.empty() ? std::string("Loading charts...") : charts.empty() ? std::format("No charts found in {}", SettingsManager::settings.list_song_directory)
                                                                                                                                       : std::string("No charts match your search"),
                      {screensize.x / 48, screensize.y / 2}, olc::GREY);
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
        float widest_grade = 0;

        for (Grade grade : {Grade::X, Grade::S, Grade::A, Grade::B, Grade::C, Grade::D, Grade::E}) {
            widest_grade = std::max(widest_grade, (float)text_bounds(skin->display_font_large, std::string(grade_name(grade))).size.x);
        }

        float grade_indent = std::max(0.0f, GRADE_SCREEN_MARGIN + widest_grade + GRADE_GAP - list_margin);
        const ProgressBar difficulty_bar({DIFFICULTY_BAR_WIDTH, DIFFICULTY_BAR_HEIGHT});
        int32_t difficulty_budget = DIFFICULTY_CALCULATIONS_PER_FRAME;

        field->draw(window);

        for (int32_t index = selected_chart_index - (charts_per_screen / 2 + 5); index < selected_chart_index + (charts_per_screen / 2 + 5); index++) {
            size_t chart_index = visible[positive_modulo(index, visible.size())];
            Chart* chart = charts[chart_index].get();

            float y = (float)screensize.y / 2 + (index - selected_chart_index) * list_chart_height + list_scroll_offset;
            float x = list_margin + grade_indent + std::pow(std::max(300.0 - std::abs((float)screensize.y / 2 - (y)), 0.0) / 50.0, 2);

            draw_text(skin->display_font_medium, std::format("{}", chart->info.title), {(int)x, (int)y}, index == selected_chart_index ? olc::WHITE : olc::GREY);

            draw_text(skin->display_font_small, std::format("[{}] - {} // {}", chart->info.difficulty_name, chart->info.artist, chart->info.mapper), {(int)x, (int)y + 36}, index == selected_chart_index ? olc::WHITE : olc::GREY);

            std::optional<float> rating = get_difficulty(chart_index, difficulty_budget);
            float fill = rating ? std::clamp(*rating / DIFFICULTY_BAR_MAX, 0.0f, 1.0f) : 0.0f;
            olc::Pixel bar_color = skin->title_gradient_at(fill, 0.5);
            bar_color.a = index == selected_chart_index ? 255 : 140;
            difficulty_bar.draw(window, {x, y + 50}, fill, bar_color);

            if (rating) {
                draw_text(skin->display_font_small, std::format("{:.2f}", *rating), {(int)(x + DIFFICULTY_BAR_WIDTH + 12), (int)y + 58}, index == selected_chart_index ? olc::WHITE : olc::GREY);
            }

            int64_t length_ms = std::llround((double)chart->get_duration_ms() * chart->pitch / pitch);
            draw_text(skin->display_font_small, std::format("{}:{:02}", length_ms / 60000, (length_ms / 1000) % 60), {(int)(x + DIFFICULTY_BAR_WIDTH + LENGTH_OFFSET), (int)y + 58}, index == selected_chart_index ? olc::WHITE : olc::GREY);

            std::optional<LocalBest> best = LocalBests::lookup(chart_hashes[chart_index], pitch);

            if (best) {
                Grade grade = grade_for_accuracy(best->accuracy);
                std::string grade_text = std::string(grade_name(grade));
                olc::FontRect grade_bounds = text_bounds(skin->display_font_large, grade_text);
                float grade_center_y = y + 18;
                draw_text(skin->display_font_large, grade_text, {(int)(x - GRADE_GAP - grade_bounds.size.x - grade_bounds.offset.x), (int)(grade_center_y - grade_bounds.size.y / 2 - grade_bounds.offset.y)}, grade_color(grade));
            }
        }
    }

    std::optional<float> SceneList::get_difficulty(size_t index, int32_t& budget) {
        if (chart_difficulties.size() != charts.size()) {
            chart_difficulties.assign(charts.size(), std::nullopt);
        }

        if (!chart_difficulties[index] && budget > 0) {
            budget--;
            charts[index]->set_pitch(pitch);
            chart_difficulties[index] = (float)DifficultyCalculator::calculate(*charts[index]);
        }

        return chart_difficulties[index];
    }

    void SceneList::set_pitch(float pitch) {
        this->pitch = snap_pitch(pitch);
        status_bar.set_rate(this->pitch);
        chart_difficulties.assign(charts.size(), std::nullopt);
        refresh_search();

        if (!visible.empty()) {
            select_chart(selected_chart_index);
        }
    }
}
