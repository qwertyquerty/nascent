#pragma once

#include "olcPixelGameEngine.h"
#include "boost/filesystem.hpp"
#include "Game.h"

#include <vector>

namespace nascent {
    class Skin {
        protected:
        const std::string skins_directory = "assets/skins";
        boost::filesystem::path skin_path;

        public:
        std::string name;
        std::vector<olc::Sprite*> note_skin_sprites;
        std::vector<olc::Decal*> note_skin_decals;
        olc::Sprite* title_gradient;
        uint8_t key_count;

        std::vector<int32_t> menu_pluck_sfx;
        int32_t hitsound_sfx = -1;

        Skin(Game* game, std::string name, uint8_t key_count);
        ~Skin();
        void load_sprites(Game* game);
        void load_sounds(Game* game);

        olc::Pixel title_gradient_at(double x, double y);
        int32_t get_random_menu_pluck();
    };
}
