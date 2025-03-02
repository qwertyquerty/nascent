#include "SceneTitle.h"

namespace nascent {
    SceneTitle::SceneTitle() {
        
    };

    SceneTitle::~SceneTitle() {
        delete title_skin;
    }

    void SceneTitle::init(Game* game) {
        title_skin = new Skin("default");
        logo_chart = new Chart("C:\\Users\\Madeline\\Desktop\\nascent\\assets\\songs\\1134132 Camellia as Reverse of Riot - Completeness Under Incompleteness (true prooF Long ver)\\Camellia as Reverse of Riot - Completeness Under Incompleteness (true prooF Long ver.) (Monheim) [Spectral].osu.json");

        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i ++) {
            logo_field[i] = new EntityField(logo_chart, title_skin, {1920/(SCENE_TITLE_FIELD_COUNT-1)*i, 0}, {1920/(SCENE_TITLE_FIELD_COUNT-1), 1080});
        }
        

        logo_audio_id = game->get_audio().LoadSound(logo_chart->audio_path.string());

        game->get_audio().Play(logo_audio_id);
    };

    void SceneTitle::update(Game* game, float elapsed_time) {
        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i ++) {
            logo_field[i]->pos.x -= elapsed_time * SCENE_TITLE_SCROLL_RATE_PX_PER_S;
            logo_field[i]->update_song_position(game->get_audio().GetCursorMilliseconds(logo_audio_id), elapsed_time);
        }

        if (logo_field[1]->pos.x <= 0) {
            for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i ++) {
                logo_field[i]->pos.x = 1920/(SCENE_TITLE_FIELD_COUNT-1)*i;
            }
        }
    };

    void SceneTitle::draw(olc::PixelGameEngine* window) {
        for (uint8_t i = 0; i < SCENE_TITLE_FIELD_COUNT; i ++) {
            logo_field[i]->draw(window);
        }
    };
}
