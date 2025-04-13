#include "ScenePlay.h"
#include "InputManager.h"

namespace nascent {
    ScenePlay::ScenePlay() {
        
    };

    ScenePlay::~ScenePlay() {
        delete skin;
        delete field;
        delete chart;
    }

    void ScenePlay::init(Game* game) {
        olc::vi2d window_size = game->window->GetWindowSize();

        skin = new Skin("default", 4);
        chart = new Chart(R"(assets\songs\Jack - v1\dimtrip.json)");

        double lane_width = window_size.x/12;

        field = new EntityField(chart, skin, {lane_width * chart->info.key_count, 0}, {(double)window_size.x/3, (double)window_size.y});
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = lane_width/2;

        chart_audio_id = game->get_audio().LoadSound(chart->audio_path.string());
        //game->get_audio().SetPitch(chart_audio_id, 0.8);
        
    };

    void ScenePlay::update(Game* game, float elapsed_time) {
        int32_t position = 0;
        
        if (!chart_started) {
            if (timer >= PLAY_START_DELAY_S) {
                chart_started = true;
                game->get_audio().Play(chart_audio_id);
            }

            position = ((float)(-PLAY_START_DELAY_S) + timer) * 1000;
            timer += elapsed_time;
        }
        else {
            position = game->get_audio().GetCursorMilliseconds(chart_audio_id);
        }

        field->update_song_position(position, elapsed_time);
        field->set_keys(InputManager::get_lane_keys_pressed(game->window));
        field->update(this, elapsed_time);

        timer += elapsed_time;
    };

    void ScenePlay::draw(olc::PixelGameEngine* window) {
        field->draw(window);
        window->DrawStringDecal({20, 20}, std::format("ACC: {:.2f}%", field->attempt->get_accuracy() * 100), olc::WHITE, {4,4});
        window->DrawStringDecal({20, 60}, std::format("ERR: {:.1f}ms", field->attempt->get_avg_err()), olc::WHITE, {4,4});

        uint8_t l = 0;
        for (auto it = field->attempt->judged_hits.rbegin(); it != field->attempt->judged_hits.rend(); ++it)
        {
            if ((*it)->played) {
                JudgedHit* jhit = *it;
                window->DrawStringDecal({20.0, 120 + 30*l}, std::format("{}", HIT_SCORE_NAME.at(jhit->score)), olc::Pixel(HIT_SCORE_COLOR.at(jhit->score)), {3, 3});
                window->DrawStringDecal({260.0, 120 + 30*l}, std::format("{:+}ms", jhit->err), olc::WHITE, {3,3});
                l += 1;
            }

            if (l == 30) {
                break;
            }
        }
    };
}
