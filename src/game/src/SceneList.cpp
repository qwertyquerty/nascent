#include "Const.h"
#include "SceneList.h"
#include "Chart.h"
#include "DifficultyCalculator.h"
#include "EntityField.h"
#include "Game.h"
#include "InputManager.h"
#include "KeyEvents.h"
#include "ProgressBar.h"
#include "Scene.h"
#include "Skin.h"
#include "Util.h"
#include "SceneAccount.h"
#include "SceneCalibration.h"
#include "ScenePlay.h"
#include "SettingsManager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <utility>

namespace nascent {
    static constexpr float DIFFICULTY_BAR_MAX = 100.0f;
    static constexpr int32_t DIFFICULTY_CALCULATIONS_PER_FRAME = 4;
    static constexpr float DIFFICULTY_BAR_WIDTH = 300.0f;
    static constexpr float DIFFICULTY_BAR_HEIGHT = 6.0f;

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
    };

    void SceneList::on_enter() {
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
            game->set_scene(std::make_unique<SceneCalibration>(skin, this));
            return;
        }

        if (game->window->GetKey(InputManager::account_key).bPressed) {
            game->set_scene(std::make_unique<SceneAccount>(skin, this));
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
        };

        std::vector<LoadedChart> loaded;

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

            for (const auto& chart_file : std::filesystem::directory_iterator(chart_folder.path(), ec)) {
                if (chart_file.path().extension() != CHART_FILE_EXTENSION) {
                    continue;
                }

                std::string error;
                std::unique_ptr<Chart> chart = Chart::load(chart_file.path(), pitch, error);

                if (chart == nullptr) {
                    std::cerr << "Skipping chart " << chart_file.path().string() << ": " << error << std::endl;
                    continue;
                }

                float difficulty = (float)DifficultyCalculator::calculate(*chart);
                loaded.push_back({std::move(chart), folder, difficulty});
            }
        }

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
        chart_difficulties.clear();

        for (LoadedChart& entry : loaded) {
            charts.push_back(std::move(entry.chart));
            chart_difficulties.push_back(entry.difficulty);
        }
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
        const ProgressBar difficulty_bar({DIFFICULTY_BAR_WIDTH, DIFFICULTY_BAR_HEIGHT});
        int32_t difficulty_budget = DIFFICULTY_CALCULATIONS_PER_FRAME;

        field->draw(window);

        for (int32_t index = selected_chart_index - (charts_per_screen / 2 + 5); index < selected_chart_index + (charts_per_screen / 2 + 5); index++) {
            Chart* chart = charts[positive_modulo(index, charts.size())].get();

            float y = (float)screensize.y / 2 + (index - selected_chart_index) * list_chart_height + list_scroll_offset;
            float x = (float)screensize.x / 48 + std::pow(std::max(300.0 - std::abs((float)screensize.y / 2 - (y)), 0.0) / 50.0, 2);

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
        chart_difficulties.assign(charts.size(), std::nullopt);
        select_chart(selected_chart_index);
    }
}
