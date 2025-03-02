#include "Skin.h"

namespace nascent {
    Skin::Skin(std::string name) {
        this->name = name;
        load_sprites();
    }

    Skin::~Skin() {
        for (uint8_t i = 0; i < 4; i++) {
            delete note_skin_sprites[i];
            delete note_skin_decals[i];
        }
    }

    void Skin::load_sprites() {
        skin_path = boost::filesystem::path(skins_directory) /= name;

        // Load noteskins
        for (uint8_t i = 0; i < 4; i++) {
            note_skin_sprites.push_back(new olc::Sprite((skin_path / std::format("note_{}.png", i+1)).string()));
            note_skin_decals.push_back(new olc::Decal(note_skin_sprites.back()));
        }
    }
}