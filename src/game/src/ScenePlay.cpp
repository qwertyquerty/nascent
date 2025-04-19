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

        skin = new Skin(game, "default", 4);
        chart = new Chart(R"(assets\songs\bakunana\ARM feat. Nanahira - BakunanaTestroyer (Rinzler) [BEAST].osu.json)");

        double lane_width = window_size.x/12;

        field = new EntityField(chart, skin, {lane_width * chart->info.key_count, 0}, {(double)window_size.x/3, (double)window_size.y});
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = lane_width/2;
        field->audio_visual_offset = FIELD_AUDIO_VISUAL_OFFSET_MS;
        field->audio_input_offset = FIELD_AUDIO_INPUT_OFFSET_MS;

        chart_audio_id = game->get_audio().LoadSound(chart->audio_path.string());
        //game->get_audio().SetPitch(chart_audio_id, 0.8);

        game->get_audio().Play(skin->get_random_menu_pluck());
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

        frame_pos = game->get_audio().GetCursorFrames(chart_audio_id);

        field->update_song_position(position, elapsed_time);

        uint32_t held_keys = InputManager::get_lane_keys_pressed(game->window);
        field->set_keys(held_keys);

        field->update(this, elapsed_time);

        timer += elapsed_time;
    };

    void ScenePlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        field->draw(window);
        window->DrawStringDecal({20, 20}, std::format("ACC: {:.2f}%", field->attempt->get_accuracy() * 100), olc::WHITE, {4,4});
        window->DrawStringDecal({20, 60}, std::format("ERR: {:.1f}ms", field->attempt->get_avg_err()), olc::WHITE, {4,4});
        window->DrawStringDecal({20, 100}, std::format("COM: {}X", field->attempt->get_combo()), olc::WHITE, {4,4});
        window->DrawStringDecal({20, 140}, std::format("FT: {}", frame_pos), olc::WHITE, {4,4});

        uint8_t l = 0;
        for (auto it = field->attempt->judged_hits.rbegin(); it != field->attempt->judged_hits.rend(); ++it)
        {
            if ((*it)->hit) {
                JudgedHit* jhit = *it;
                window->DrawStringDecal({20.0, 200 + 30*l}, std::format("{}", HIT_SCORE_NAME.at(jhit->hit_score)), olc::Pixel(HIT_SCORE_COLOR.at(jhit->hit_score)), {3, 3});
                window->DrawStringDecal({260.0, 200 + 30*l}, std::format("{:+}ms", jhit->hit_err), olc::WHITE, {3,3});
                
                l += 1;
                if (l >= 30) {
                    break;
                }

                if (jhit->chart_hit.hit_type == HitType::HOLD) {
                    if (jhit->released) {
                        window->DrawStringDecal({20.0, 200 + 30*l}, std::format("{}*", HIT_SCORE_NAME.at(jhit->release_score)), olc::Pixel(HIT_SCORE_COLOR.at(jhit->release_score)), {3, 3});
                        window->DrawStringDecal({260.0, 200 + 30*l}, std::format("{:+}ms", jhit->release_err), olc::WHITE, {3,3});
                    }
                    else {
                        window->DrawStringDecal({20.0, 200 + 30*l}, "...", olc::WHITE, {3, 3});
                    }
                    l += 1;
                }
            }

            if (l >= 28) {
                break;
            }
        }

        l = 0;
        
        float hit_width = screensize.x / 400;
        float hit_height = hit_width * 15;
        float px_per_ms = ((double)screensize.x) / 3.0 / get_window_from_hit_acc_diff(HitScore::BAD, field->chart->info.hit_accuracy) / 2;
        float y = screensize.y - hit_height;

        for (HitScore h = HitScore::BAD; h >= HitScore::PERFECT; h = (HitScore)(h-1)) {
            uint16_t hit_err_window = get_window_from_hit_acc_diff(h, field->chart->info.hit_accuracy);
            float x = screensize.x / 2 - hit_err_window * px_per_ms;
            olc::Pixel c = HIT_SCORE_COLOR.at(h);
            c.a = 24;
            window->FillRectDecal({x, y}, {hit_err_window * 2 * px_per_ms, hit_height}, c);
        }

        for (auto it = field->attempt->judged_hits.rbegin(); it != field->attempt->judged_hits.rend(); ++it) {
            if ((*it)->hit_played && (*it)->hit_score != HitScore::MISS) {
                JudgedHit* jhit = *it;
                float x = screensize.x / 2 - hit_width-2 + jhit->hit_err * px_per_ms;
                window->FillRectDecal({x, y}, {hit_width, hit_height}, olc::Pixel(200, 200, 255, 64));
                l++;
            }

            if (l >= 100) {
                break;
            }
        }
    };
}
