#include "Skin.h"

namespace nascent {
    Skin::Skin(std::string name, uint8_t key_count) {
        this->name = name;
        this->key_count = key_count;
        load_sprites();
    }

    Skin::~Skin() {
        for (uint8_t i = 0; i < key_count; i++) {
            delete note_skin_sprites[i];
            delete note_skin_decals[i];
        }

        delete title_gradient;
    }

    void Skin::load_sprites() {
        skin_path = boost::filesystem::path(skins_directory) /= name;

        // Load noteskins
        for (uint8_t i = 0; i < key_count; i++) {
            note_skin_sprites.push_back(new olc::Sprite((skin_path / std::format("note_{}.png", i+1)).string()));
            note_skin_decals.push_back(new olc::Decal(note_skin_sprites.back()));
        }

        title_gradient = new olc::Sprite((skin_path / "title_gradient.png").string());
    }

    olc::Pixel Skin::title_gradient_at(double x, double y) {
        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        return title_gradient->GetPixel({(int32_t)(x * (title_gradient->width-1)), (int32_t)(y * (title_gradient->height-1))});
    }
}