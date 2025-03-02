#pragma once

#include "olcPixelGameEngine.h"
#include "boost/filesystem.hpp"

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

        Skin(std::string name);
        ~Skin();
        void load_sprites();
    };
}
