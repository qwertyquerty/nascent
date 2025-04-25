#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"
#include "boost/filesystem.hpp"
#include "Game.h"
#include "Accuracy.h"

#include <vector>

namespace nascent {
    class Skin {
        protected:
        const std::string skins_directory = "assets/skins";
        boost::filesystem::path skin_path;

        public:
        uint16_t display_font_large_size = 72;
        olc::Font display_font_large;

        uint16_t display_font_medium_size = 36;
        olc::Font display_font_medium;

        uint16_t display_font_small_size = 18;
        olc::Font display_font_small;

        olc::Font interface_font;
        olc::Font mono_font;

        std::map<HitScore, olc::Decal*> hit_score_decals;

        std::string name;
        std::vector<olc::Sprite*> note_skin_sprites;
        std::vector<olc::Decal*> note_skin_decals;
        olc::Sprite* title_gradient;
        uint8_t key_count;

        double field_key_spacing_ratio = 0.15;

        uint8_t hold_alpha = 64;

        olc::Decal* game_paused_decal;

        std::vector<int32_t> menu_pluck_sfx;
        int32_t hitsound_sfx = -1;
        int32_t idle_loop_sfx = -1;

        Skin(Game* game, std::string name, uint8_t key_count);
        ~Skin();
        void load_sprites(Game* game);
        void load_sounds(Game* game);

        olc::Pixel title_gradient_at(double x, double y);
        int32_t get_random_menu_pluck();
    };
}
