#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"
#include "Accuracy.h"
#include "Util.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class Game;

    class Skin {
        protected:
        const std::filesystem::path skins_directory = "assets/skins";
        const std::filesystem::path symbol_font_path = "assets/fonts/OpenArrow-Regular.ttf";
        std::filesystem::path skin_path;
        Game* game;

        uint16_t display_font_large_size = 72;
        uint16_t display_font_medium_size = 36;
        uint16_t display_font_small_size = 18;

        std::array<SpriteDecal, HIT_SCORE_COUNT> hit_score_decals;
        std::vector<SpriteDecal> note_skin_decals;
        std::unique_ptr<olc::Sprite> title_gradient;
        std::vector<int32_t> menu_pluck_sfx;

        std::string name;
        uint8_t key_count;

        void load_sprites();
        void load_sounds();

        public:
        olc::Font display_font_large;
        olc::Font display_font_medium;
        olc::Font display_font_small;

        double field_key_spacing_ratio = 0.15;

        uint8_t hold_alpha = 64;

        SpriteDecal game_paused_decal;

        int32_t hitsound_sfx = -1;
        int32_t idle_loop_sfx = -1;

        Skin(Game* game, std::string name, uint8_t key_count);
        ~Skin();
        Skin(const Skin&) = delete;
        Skin& operator=(const Skin&) = delete;

        olc::Decal* note_decal(uint8_t key) const;
        olc::Decal* hit_score_decal(HitScore score) const;
        olc::Pixel title_gradient_at(double x, double y);
        int32_t get_random_menu_pluck();
    };
}
