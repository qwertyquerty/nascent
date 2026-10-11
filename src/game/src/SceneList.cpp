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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <thread>
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
    }

    void SceneList::init(Game*) {
        load_charts();
        backfill_bests();
    };

    void SceneList::backfill_bests() {
        if (LocalBests::is_backfilled()) {
            return;
        }

        std::unordered_map<std::string, std::string> chart_hash_by_folder;

        for (size_t index = 0; index < charts.size(); index++) {
            std::optional<std::string> file_hash = sha256_file_hex(charts[index]->file_path);

            if (file_hash) {
                chart_hash_by_folder[file_hash->substr(0, Replay::FOLDER_HASH_LENGTH)] = chart_hashes[index];
            }
        }

        LocalBests::backfill(chart_hash_by_folder, user_config_directory(SettingsManager::APP_NAME) / "replays");
    }

    void SceneList::on_enter() {
        if (BeatmapDownloads::take_library_changed()) {
            reload_charts();
        }

        if (!charts.empty()) {
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

        if (charts.empty()) {
            return;
        }

        if (game->window->GetKey(InputManager::random_chart_key).bPressed) {
            select_chart(rand() % charts.size());
        }

        if (game->window->GetKey(InputManager::menu_down_key).bPressed) {
            select_chart(positive_modulo((selected_chart_index + 1), charts.size()));
        }

        if (game->window->GetKey(InputManager::menu_up_key).bPressed) {
            select_chart(positive_modulo((selected_chart_index - 1), charts.size()));
        }

        int mwheel = game->window->GetMouseWheel();
        if (mwheel != 0) {
            select_chart(positive_modulo(
                selected_chart_index + ((mwheel < 0) - (mwheel > 0)),
                charts.size()
            ));
        }

        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            game->set_scene(std::make_unique<ScenePlay>(selected_chart, skin, this));
        }

        if (game->window->GetKey(InputManager::keybind_mod1).bHeld) {
            if (game->window->GetKey(InputManager::menu_left_key).bPressed) {
                set_pitch(pitch - PITCH_INTERVAL);
            }
            if (game->window->GetKey(InputManager::menu_right_key).bPressed) {
                set_pitch(pitch + PITCH_INTERVAL);
            }
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

    void SceneList::select_chart(int32_t index) {
        selected_chart = charts[index].get();
        status_bar.set_key_count(selected_chart->info.key_count);

        selected_chart->set_pitch(this->pitch);

        int32_t down_steps = (index - selected_chart_index + charts.size()) % charts.size();
        int32_t up_steps = (selected_chart_index - index + charts.size()) % charts.size();

        list_scroll_offset += std::clamp(
            (up_steps < down_steps ? -up_steps : down_steps) * list_chart_height,
            list_chart_height * -5,
            list_chart_height * 5
        );

        selected_chart_index = index;

        game->play_sound(skin->hitsound_sfx);

        olc::vi2d window_size = game->window->GetWindowSize();
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

    void SceneList::load_charts() {
        std::error_code ec;
        std::filesystem::path song_directory = SettingsManager::settings.list_song_directory;

        if (!std::filesystem::is_directory(song_directory, ec)) {
            return;
        }

        struct LoadedChart {
            std::unique_ptr<Chart> chart;
            std::u8string folder;
            float difficulty;
            std::string hash;
        };

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

            std::u8string folder = chart_folder.path().filename().u8string();

            for (char8_t& c : folder) {
                if (c >= u8'A' && c <= u8'Z') {
                    c = (char8_t)(c - u8'A' + u8'a');
                }
            }

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

            if (chart == nullptr) {
                std::lock_guard<std::mutex> lock(log_mutex);
                std::cerr << "Skipping chart " << chart_files[index].path.string() << ": " << error << std::endl;
                return;
            }

            std::string hash = chart->gameplay_hash();
            parsed[index] = {std::move(chart), chart_files[index].folder, 0.0f, std::move(hash)};
        });

        std::vector<LoadedChart> loaded;
        std::unordered_set<std::string> loaded_hashes;

        for (LoadedChart& entry : parsed) {
            if (entry.chart != nullptr && loaded_hashes.insert(entry.hash).second) {
                loaded.push_back(std::move(entry));
            }
        }

        parallel_for(loaded.size(), [&](size_t index) {
            loaded[index].difficulty = (float)DifficultyCalculator::calculate(*loaded[index].chart);
        });

        std::stable_sort(loaded.begin(), loaded.end(), [](const LoadedChart& a, const LoadedChart& b) {
            if (a.folder != b.folder) {
                return a.folder < b.folder;
            }

            if (a.difficulty != b.difficulty) {
                return a.difficulty < b.difficulty;
            }

            return a.chart->info.difficulty_name < b.chart->info.difficulty_name;
        });

        charts.clear();
        chart_hashes.clear();
        chart_difficulties.clear();

        for (LoadedChart& entry : loaded) {
            charts.push_back(std::move(entry.chart));
            chart_hashes.push_back(std::move(entry.hash));
            chart_difficulties.push_back(entry.difficulty);
        }

        local_hashes = std::move(loaded_hashes);
    }

    void SceneList::reload_charts() {
        std::string selected_hash = selected_chart != nullptr ? selected_chart->gameplay_hash() : std::string();
        selected_chart = nullptr;
        preview_audio_chart = nullptr;
        load_charts();
        selected_chart_index = 0;

        for (size_t index = 0; index < charts.size(); index++) {
            if (charts[index]->gameplay_hash() == selected_hash) {
                selected_chart_index = (int32_t)index;
                break;
            }
        }
    }

    const std::unordered_set<std::string>& SceneList::get_local_hashes() const {
        return local_hashes;
    }

    void SceneList::draw(olc::PixelGameEngine* window) {
        draw_list(window);
        status_bar.draw(window, skin);
    }

    void SceneList::draw_list(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        if (charts.empty()) {
            skin->display_font_medium.DrawString(
                std::format("No charts found in {}", SettingsManager::settings.list_song_directory),
                {screensize.x / 48, screensize.y / 2},
                olc::GREY
            );
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
            widest_grade = std::max(widest_grade, (float)skin->display_font_large.GetStringBounds(std::string(grade_name(grade))).size.x);
        }

        float grade_indent = std::max(0.0f, GRADE_SCREEN_MARGIN + widest_grade + GRADE_GAP - list_margin);
        const ProgressBar difficulty_bar({DIFFICULTY_BAR_WIDTH, DIFFICULTY_BAR_HEIGHT});
        int32_t difficulty_budget = DIFFICULTY_CALCULATIONS_PER_FRAME;

        field->draw(window);

        for (int32_t index = selected_chart_index - (charts_per_screen / 2 + 5); index < selected_chart_index + (charts_per_screen / 2 + 5); index++) {
            Chart* chart = charts[positive_modulo(index, charts.size())].get();

            float y = (float)screensize.y / 2 + (index - selected_chart_index) * list_chart_height + list_scroll_offset;
            float x = list_margin + grade_indent + std::pow(std::max(300.0 - std::abs((float)screensize.y / 2 - (y)), 0.0) / 50.0, 2);

            skin->display_font_medium.DrawString(
                std::format("{}", chart->info.title),
                {(int)x, (int)y},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );

            skin->display_font_small.DrawString(
                std::format("[{}] - {} // {}", chart->info.difficulty_name, chart->info.artist, chart->info.mapper),
                {(int)x, (int)y + 36},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );

            std::optional<float> rating = get_difficulty(positive_modulo(index, charts.size()), difficulty_budget);
            float fill = rating ? std::clamp(*rating / DIFFICULTY_BAR_MAX, 0.0f, 1.0f) : 0.0f;
            olc::Pixel bar_color = skin->title_gradient_at(fill, 0.5);
            bar_color.a = index == selected_chart_index ? 255 : 140;
            difficulty_bar.draw(window, {x, y + 50}, fill, bar_color);

            if (rating) {
                skin->display_font_small.DrawString(
                    std::format("{:.2f}", *rating),
                    {(int)(x + DIFFICULTY_BAR_WIDTH + 12), (int)y + 58},
                    index == selected_chart_index ? olc::WHITE : olc::GREY
                );
            }

            int64_t length_ms = std::llround((double)chart->get_duration_ms() * chart->pitch / pitch);
            skin->display_font_small.DrawString(
                std::format("{}:{:02}", length_ms / 60000, (length_ms / 1000) % 60),
                {(int)(x + DIFFICULTY_BAR_WIDTH + LENGTH_OFFSET), (int)y + 58},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );

            std::optional<LocalBest> best = LocalBests::lookup(chart_hashes[positive_modulo(index, charts.size())], pitch);

            if (best) {
                Grade grade = grade_for_accuracy(best->accuracy);
                std::string grade_text = std::string(grade_name(grade));
                olc::FontRect grade_bounds = skin->display_font_large.GetStringBounds(grade_text);
                float grade_center_y = y + 18;
                skin->display_font_large.DrawString(
                    grade_text,
                    {(int)(x - GRADE_GAP - grade_bounds.size.x - grade_bounds.offset.x), (int)(grade_center_y - grade_bounds.size.y / 2 - grade_bounds.offset.y)},
                    grade_color(grade)
                );
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
        select_chart(selected_chart_index);
    }
}
