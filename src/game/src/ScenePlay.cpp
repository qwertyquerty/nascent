#include "ScenePlay.h"
#include "InputManager.h"
#include "Util.h"

namespace nascent {
    ScenePlay::ScenePlay(Chart* chart, Skin* skin) {
        this->chart = chart;
        this->skin = skin;
    };

    ScenePlay::~ScenePlay() {
        delete skin;
        delete field;
        delete chart;
    }

    void ScenePlay::init(Game* game) {
        olc::vi2d window_size = game->window->GetWindowSize();

        double lane_width = window_size.x/12;

        field = new EntityField(chart, skin, {lane_width * chart->info.key_count, 0}, {(double)window_size.x/3, (double)window_size.y});
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = lane_width/2;
        field->audio_visual_offset = FIELD_AUDIO_VISUAL_OFFSET_MS;
        field->audio_input_offset = FIELD_AUDIO_INPUT_OFFSET_MS;

        timer = 0;

        chart_audio_id = game->get_audio().LoadSound(chart->audio_path.string());
        //game->get_audio().SetPitch(chart_audio_id, 0.8);

        game->get_audio().Play(skin->get_random_menu_pluck());
    };

    void ScenePlay::update(float elapsed_time) {
        int32_t position = 0;
        
        if (!chart_started) {
            if (timer >= PLAY_START_DELAY_S) {
                chart_started = true;
                game->get_audio().Play(chart_audio_id);
            }

            position = ((float)(-PLAY_START_DELAY_S) + timer) * 1000;
        }
        else {
            position = game->get_audio().GetCursorMilliseconds(chart_audio_id);
        }

        frame_pos = game->get_audio().GetCursorFrames(chart_audio_id);

        if(paused) {
            if (game->window->GetKey(InputManager::pause_key).bPressed) {
                paused = false;
                if (chart_started) {
                    game->get_audio().Play(chart_audio_id);
                }
                game->get_audio().Stop(skin->idle_loop_sfx);
            }
        }
        else {
            field->update_song_position(position, elapsed_time);

            if (!game->window->IsFocused() || game->window->GetKey(InputManager::pause_key).bPressed) {
                paused = true;
                game->get_audio().Pause(chart_audio_id);
                game->get_audio().Play(skin->idle_loop_sfx, true);
            }
        }

        if(!paused){
            // Make sure arrow keys cant be registered in pause
            uint32_t held_keys = InputManager::get_lane_keys_pressed(game->window);
            field->set_keys(held_keys);
            field->update(this, elapsed_time);
            
            timer += elapsed_time;
        }
    };

    void ScenePlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        field->draw(window);
        skin->display_font_medium.DrawString(std::format("{:.2f}%", field->attempt->get_accuracy() * 100), 20, 40, olc::WHITE);
        skin->display_font_medium.DrawString(std::format("{:.1f}ms", field->attempt->get_avg_err()), 20, 80, olc::WHITE);
        skin->display_font_medium.DrawString(std::format("{}X", field->attempt->get_combo()), 20, 120, olc::WHITE);

        std::string now_playing_string = std::format("{} - {}", chart->info.artist, chart->info.title);
        uint16_t w = skin->display_font_medium.GetStringBounds(now_playing_string).size.x;
        skin->display_font_medium.DrawString(now_playing_string, {screensize.x/2 - w/2, (int)smoothstep(-screensize.x / 24, screensize.x / 24, (timer-PLAY_START_DELAY_S+2)*2)}, olc::WHITE);

        uint8_t l = 0;
        const uint8_t vspacing = 26;
        const uint8_t vlen = 35;
        for (int32_t i = field->attempt->soonest_scored_hit_index; i >= 0; i--)
        {
            JudgedHit* jhit = field->attempt->judged_hits[i];

            if (jhit->hit) {
                window->DrawStringDecal({20.0, 160 + vspacing*l}, std::format("{}", HIT_SCORE_ACC.at(jhit->hit_score)), HIT_SCORE_COLOR.at(jhit->hit_score), {2,2});
                window->DrawStringDecal({80.0, 160 + vspacing*l}, std::format("  {:+}ms", jhit->hit_err), olc::WHITE, {2,2});
                
                l += 1;
                if (l >= vlen) {
                    break;
                }

                if (jhit->chart_hit.hit_type == HitType::HOLD) {
                    if (jhit->released) {
                        window->DrawStringDecal({20.0, 160 + vspacing*l}, std::format("{}", HIT_SCORE_ACC.at(jhit->release_score)), HIT_SCORE_COLOR.at(jhit->release_score), {2,2});
                        window->DrawStringDecal({80.0, 160 + vspacing*l}, std::format("R {:+}ms", jhit->release_err), olc::WHITE, {2,2});
                    }
                    else {
                        window->DrawStringDecal({20.0, 160 + vspacing*l}, "...", olc::GREY, {2, 2});
                    }
                    l += 1;
                }
            }

            if (l >= vlen) {
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

            if (l >= 200) {
                break;
            }
        }

        if (paused) {
            window->FillRectDecal({0, 0}, screensize, olc::Pixel(0, 0, 0, 200));
            window->DrawDecal({screensize.x/2 - skin->game_paused_decal->sprite->width/2, screensize.y/2 - skin->game_paused_decal->sprite->height/2}, skin->game_paused_decal);
        }
    };
}
