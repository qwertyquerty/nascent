#include "Skin.h"
#include "Game.h"
#include "Accuracy.h"

namespace nascent {
    Skin::Skin(Game* game, std::string name, uint8_t key_count) {
        this->name = name;
        this->key_count = key_count;
        load_sprites(game);
        load_sounds(game);
    }

    Skin::~Skin() {
        for (uint8_t i = 0; i < key_count; i++) {
            delete note_skin_sprites[i];
            delete note_skin_decals[i];
        }

        for (HitScore h = HitScore::PERFECT; h <= HitScore::NONE; h = (HitScore)(h + 1)) {
            delete hit_score_decals[h];
        }

        delete title_gradient;
    }

    void Skin::load_sprites(Game* game) {
        skin_path = boost::filesystem::path(skins_directory) /= name;

        // Load noteskins
        for (uint8_t i = 0; i < key_count; i++) {
            note_skin_sprites.push_back(new olc::Sprite((skin_path / std::format("note_{}.png", i+1)).string()));
            note_skin_decals.push_back(new olc::Decal(note_skin_sprites.back()));
        }

        title_gradient = new olc::Sprite((skin_path / "title_gradient.png").string());

        display_font_small = {(skin_path / "display_font.ttf").string(), display_font_small_size};
        display_font_medium = {(skin_path / "display_font.ttf").string(), display_font_medium_size};
        display_font_large = {(skin_path / "display_font.ttf").string(), display_font_large_size};

        for (HitScore h = HitScore::PERFECT; h <= HitScore::NONE; h = (HitScore)(h + 1)) {
            hit_score_decals[h] = display_font_large.RenderStringToDecal(HIT_SCORE_NAME.at(h), HIT_SCORE_COLOR.at(h));
        }

        game_paused_decal = display_font_large.RenderStringToDecal("Game Paused", olc::WHITE);
    }

    void Skin::load_sounds(Game* game) {
        skin_path = boost::filesystem::path(skins_directory) /= name;

        for (uint8_t i = 0; i < 10; i++) {
            if (boost::filesystem::exists(skin_path / std::format("menu_pluck_{}.mp3", i))) {
                int32_t sound = game->get_audio().LoadSound((skin_path / std::format("menu_pluck_{}.mp3", i)).string());
                menu_pluck_sfx.push_back(sound);
            }
        }

        boost::filesystem::path path_hitsound_sfx = skin_path / std::format("hitsound.mp3");
        if (boost::filesystem::exists(path_hitsound_sfx)) {
            hitsound_sfx = game->get_audio().LoadSound(path_hitsound_sfx.string());
        }

        boost::filesystem::path path_idle_loop_sfx = skin_path / std::format("idle_loop.wav");
        if (boost::filesystem::exists(path_idle_loop_sfx)) {
            idle_loop_sfx = game->get_audio().LoadSound(path_idle_loop_sfx.string());
        }
    }

    olc::Pixel Skin::title_gradient_at(double x, double y) {
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        return title_gradient->GetPixel({(int32_t)(x * (title_gradient->width-1)), (int32_t)(y * (title_gradient->height-1))});
    }

    int32_t Skin::get_random_menu_pluck() {
        return menu_pluck_sfx.at(std::rand() % menu_pluck_sfx.size());
    }
}
