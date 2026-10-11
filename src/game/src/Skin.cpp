#include "Skin.h"
#include "Game.h"
#include "Accuracy.h"

#include <format>

namespace nascent {
    Skin::Skin(Game* game, std::string name, uint8_t key_count) {
        this->game = game;
        this->name = name;
        this->key_count = key_count;
        skin_path = skins_directory / name;
        load_sprites();
        load_sounds();
    }

    Skin::~Skin() {
        for (int32_t sound : menu_pluck_sfx) {
            game->get_audio().UnloadSound(sound);
        }

        for (int32_t sound : {hitsound_sfx, idle_loop_sfx, account_loop_sfx}) {
            if (sound != -1) {
                game->get_audio().UnloadSound(sound);
            }
        }
    }

    void Skin::load_sprites() {
        // Load noteskins
        for (uint8_t i = 0; i < key_count; i++) {
            note_skin_decals.emplace_back(std::make_unique<olc::Sprite>((skin_path / std::format("note_{}.png", i + 1)).string()), true);
        }

        title_gradient = std::make_unique<olc::Sprite>((skin_path / "title_gradient.png").string());

        display_font_small = {(skin_path / "display_font.ttf").string(), display_font_small_size};
        display_font_medium = {(skin_path / "display_font.ttf").string(), display_font_medium_size};
        display_font_large = {(skin_path / "display_font.ttf").string(), display_font_large_size};

        if (std::filesystem::exists(symbol_font_path)) {
            display_font_small.AddFallbackFont(symbol_font_path.string());
            display_font_medium.AddFallbackFont(symbol_font_path.string());
            display_font_large.AddFallbackFont(symbol_font_path.string());
        }

        for (HitScore h : ALL_HIT_SCORES) {
            hit_score_decals[(size_t)h] = SpriteDecal::adopt(
                display_font_large.RenderStringToDecal(std::string(hit_score_name(h)), olc::Pixel(hit_score_color(h)))
            );
        }

        game_paused_decal = SpriteDecal::adopt(display_font_large.RenderStringToDecal("Game Paused", olc::WHITE));
    }

    void Skin::load_sounds() {
        for (uint8_t i = 0; i < 10; i++) {
            std::filesystem::path path_menu_pluck_sfx = skin_path / std::format("menu_pluck_{}.mp3", i);
            if (std::filesystem::exists(path_menu_pluck_sfx)) {
                menu_pluck_sfx.push_back(game->get_audio().LoadSound(path_menu_pluck_sfx.string()));
            }
        }

        std::filesystem::path path_hitsound_sfx = skin_path / "hitsound.mp3";
        if (std::filesystem::exists(path_hitsound_sfx)) {
            hitsound_sfx = game->get_audio().LoadSound(path_hitsound_sfx.string());
        }

        std::filesystem::path path_idle_loop_sfx = skin_path / "idle_loop.wav";
        if (std::filesystem::exists(path_idle_loop_sfx)) {
            idle_loop_sfx = game->get_audio().LoadSound(path_idle_loop_sfx.string());
        }

        std::filesystem::path path_account_loop_sfx = skin_path / "account_loop.wav";
        if (std::filesystem::exists(path_account_loop_sfx)) {
            account_loop_sfx = game->get_audio().LoadSound(path_account_loop_sfx.string());
        }
    }

    olc::Decal* Skin::note_decal(uint8_t key) const {
        return note_skin_decals.empty() ? nullptr : note_skin_decals[key % note_skin_decals.size()].get();
    }

    olc::Decal* Skin::hit_score_decal(HitScore score) const {
        return hit_score_decals[(size_t)score].get();
    }

    olc::Pixel Skin::title_gradient_at(double x, double y) {
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        return title_gradient->GetPixel({(int32_t)(x * (title_gradient->width - 1)), (int32_t)(y * (title_gradient->height - 1))});
    }

    int32_t Skin::get_random_menu_pluck() {
        if (menu_pluck_sfx.empty()) {
            return -1;
        }

        return menu_pluck_sfx.at(std::rand() % menu_pluck_sfx.size());
    }
}
