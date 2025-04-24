#include "SceneTitle.h"

namespace nascent {
    SceneTitle::SceneTitle() {
        
    };

    SceneTitle::~SceneTitle() {
        delete bg_skin;
        delete logo_skin;

        delete logo_field;

        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i ++) {
            delete bg_field[i];
        }

        delete fft_l;
        delete fft_r;

        delete logo_chart;
        delete bg_chart;
    }

    void SceneTitle::init(Game* game) {
        olc::vi2d window_size = game->window->GetWindowSize();

        logo_skin = new Skin(game, "logo", 7);
        logo_chart = new Chart(R"(assets\songs\logo\logo.json)");

        bg_skin = new Skin(game, "default", 4);
        bg_chart = new Chart(R"(assets\songs\title_2\title2.osu.json)");

        logo_field = new EntityField(logo_chart, logo_skin, {(double)window_size.x/4,0}, {(double)window_size.x/2, (double)window_size.y/2 + ((double)window_size.x/28)});
        logo_field->draw_notes_past_judge = false;
        logo_field->draw_judge_colors = true;
        logo_field->judge_alpha = 150;
        logo_field->judge_auto_active = true;
        logo_field->scroll_speed = SCENE_TITLE_SCROLL_SPEED;

        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i++) {
            bg_field[i] = new EntityField(bg_chart, bg_skin, {(double)window_size.x/(SCENE_TITLE_FIELD_COUNT-1)*i, 0}, {(double)window_size.x/(SCENE_TITLE_FIELD_COUNT-1), (double)window_size.y});
            bg_field[i]->init(this);
            bg_field[i]->debug = true;
            bg_field[i]->draw_judge = false;
            bg_field[i]->scroll_speed = SCENE_TITLE_SCROLL_SPEED;
        }

        fft_l = new EntityFFT(bg_skin, {0,0}, {(double)window_size.x, (double)window_size.y*0.28}, 0);
        fft_l->init(this);

        fft_r = new EntityFFT(bg_skin, {0,(double)window_size.y-(double)window_size.y*0.28}, {(double)window_size.x, (double)window_size.y*0.28}, 1);
        fft_r->flipped = true;
        fft_r->init(this);

        logo_audio_id = game->get_audio().LoadSound(logo_chart->audio_path.string());
        bg_audio_id = game->get_audio().LoadSound(bg_chart->audio_path.string());
    };

    void SceneTitle::update(float elapsed_time) {
        if (timer > LOGO_START_DELAY_S && !logo_started) {
            logo_started = true;
            game->get_audio().Play(logo_audio_id);
        }

        if (!game->get_audio().IsPlaying(logo_audio_id) && !game->get_audio().IsPlaying(bg_audio_id) && logo_started) {
            bg_started = true;
            game->get_audio().Play(bg_audio_id);
        }


        logo_field->update_song_position(game->get_audio().GetCursorMilliseconds(logo_audio_id), elapsed_time);
        logo_field->update(this, elapsed_time);

        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i++) {
            bg_field[i]->pos.x -= elapsed_time * SCENE_TITLE_X_SCROLL_RATE_PX_PER_S;
            bg_field[i]->update_song_position(game->get_audio().GetCursorMilliseconds(bg_audio_id), elapsed_time);
            bg_field[i]->update(this, elapsed_time);
        }

        if (game->get_audio().IsPlaying(bg_audio_id)) {
            fft_l->update_song_buffer(game->get_audio().m_engine_buffer, elapsed_time);
            fft_r->update_song_buffer(game->get_audio().m_engine_buffer, elapsed_time);
        }

        if (bg_field[1]->pos.x <= 0) {
            for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i++) {
                bg_field[i]->pos.x = 1920/(SCENE_TITLE_FIELD_COUNT-1)*i;
            }
        }

        timer += elapsed_time;
    };

    void SceneTitle::draw(olc::PixelGameEngine* window) {
        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i++) {
            bg_field[i]->draw(window);
        }

        if (bg_started) {
            logo_field->clear_keys();
            if (bg_field[0]->get_current_timing_point() != nullptr) {
                logo_field->set_key(bg_field[0]->get_section_beat() % logo_field->get_key_count());
            }
            fft_l->draw(window);
            fft_r->draw(window);
        }

        window->FillRectDecal({0, 0}, window->GetScreenSize(), {0, 0, 0, 128});

        logo_field->draw(window);
    };
}
