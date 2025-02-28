#include "SceneTitle.h"

namespace nascent {
    SceneTitle::SceneTitle() {

    };

    void SceneTitle::init(Game* game) {
        logo_chart = new Chart("C:\\Users\\Madeline\\Desktop\\nascent\\assets\\songs\\2nd Pack - v1\\Various Artists - Dan ~ REFORM ~ 2nd Pack (DDMythical) [~ 7th ~ (Marathon)].osu.json");
        logo_field = new EntityField(logo_chart);

        logo_audio_id = game->get_audio().LoadSound(logo_chart->audio_path.string());

        game->get_audio().Play(logo_audio_id);
    };

    void SceneTitle::update(Game* game, float elapsed_time) {
        logo_field->update_song_position(game->get_audio().GetCursorMilliseconds(logo_audio_id), elapsed_time);
    };

    void SceneTitle::draw(olc::PixelGameEngine* window) {
        logo_field->draw(window);
    };
}