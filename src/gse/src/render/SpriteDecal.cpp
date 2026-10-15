#include "gse/render/SpriteDecal.h"

#include <utility>

namespace nascent {
    SpriteDecal::SpriteDecal(std::unique_ptr<olc::Sprite> sprite, bool filter, bool clamp) {
        this->sprite = std::move(sprite);

        if (this->sprite != nullptr) {
            decal = std::make_unique<olc::Decal>(this->sprite.get(), filter, clamp);
        }
    }

    SpriteDecal SpriteDecal::adopt(olc::Decal* decal) {
        SpriteDecal result;

        if (decal != nullptr) {
            result.sprite.reset(decal->sprite);
            result.decal.reset(decal);
        }

        return result;
    }

    olc::Decal* SpriteDecal::get() const {
        return decal.get();
    }

    olc::Sprite* SpriteDecal::get_sprite() const {
        return sprite.get();
    }

    SpriteDecal::operator bool() const {
        return decal != nullptr;
    }

    void SpriteDecal::reset() {
        decal.reset();
        sprite.reset();
    }
}
