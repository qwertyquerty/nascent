#include "Const.h"
#include "SceneList.h"
#include "InputManager.h"
#include "Util.h"

#include <iostream>

#include "boost/filesystem.hpp"
#include <boost/range/iterator_range.hpp>

using namespace boost::filesystem;

namespace nascent {
    SceneList::SceneList() {
        
    };

    SceneList::~SceneList() {
        delete skin;
        delete field;
        
        for (Chart* chart : charts) {
            delete chart;
        }
    }

    void SceneList::init(Game* game) {
        skin = new Skin(game, "default", 4);

        load_charts();

        if (charts.size()) {
            select_chart(charts.front());
        }

        timer = 0;
    };

    void SceneList::update(float elapsed_time) {
        if (game->window->GetKey(InputManager::random_chart_key).bPressed) {
            select_chart(charts[rand() % charts.size()]);
        }

        selected_chart_position = game->get_audio().GetCursorMilliseconds(selected_chart_audio_id);
        field->update_song_position(selected_chart_position, elapsed_time);
        timer += elapsed_time;
    };

    void SceneList::select_chart(Chart* chart) {
        printf("Loading chart %s\n", chart->info.title.c_str());
        if (selected_chart_audio_id != -1) {
            game->get_audio().UnloadSound(selected_chart_audio_id);
        }

        selected_chart = chart;
        selected_chart_audio_id = game->get_audio().LoadSound(selected_chart->audio_path.string());

        delete field;
        olc::vi2d window_size = game->window->GetWindowSize();
        field = new EntityField(selected_chart, skin, {0, 0}, {(double)(window_size.x/4), (double)window_size.y});
        field->update_song_position(selected_chart->info.preview_time, 0);

        game->get_audio().Play(selected_chart_audio_id, true);
        game->get_audio().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
    }

    void SceneList::load_charts() {
        if (is_directory(LIST_SONG_DIRECTORY)) {
            for (auto& chart_folder : boost::make_iterator_range(directory_iterator(LIST_SONG_DIRECTORY), {})) {
                if (is_directory(chart_folder)) {
                    for (auto& chart_file : boost::make_iterator_range(directory_iterator(chart_folder), {})) {
                        std::string chart_file_ext = chart_file.path().extension().string();
                        if (chart_file_ext == CHART_FILE_EXTENSION) {
                            charts.push_back(new Chart(chart_file.path().string()));
                        }
                    }
                }
            }
        }
    }

    void SceneList::draw(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        field->draw(window);
    }
}
