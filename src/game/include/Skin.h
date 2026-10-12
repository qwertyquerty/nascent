#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"
#include "Accuracy.h"
#include "Util.h"
#include "json.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Game;

    enum class SkinImage : uint8_t {
        NOTE,
        RECEPTOR,
        RECEPTOR_PRESSED,
        HOLD_HEAD,
        HOLD_BODY,
        HOLD_CAP
    };

    inline constexpr size_t SKIN_IMAGE_COUNT = 6;

    enum class SkinSound : uint8_t {
        HITSOUND,
        IDLE_LOOP,
        ACCOUNT_LOOP
    };

    inline constexpr size_t SKIN_SOUND_COUNT = 3;

    enum class SkinTint : uint8_t {
        GRADIENT,
        NONE
    };

    enum class HoldStyle : uint8_t {
        REPEAT,
        STRETCH
    };

    struct SkinManifest {
        std::string name;
        std::string inherits;
        bool hidden = false;
        SkinTint tint = SkinTint::GRADIENT;
        HoldStyle hold_style = HoldStyle::REPEAT;
        double key_spacing = 0.15;
        uint8_t hold_alpha = 64;
        std::optional<uint8_t> receptor_alpha;
        std::map<uint8_t, std::vector<uint8_t>> lane_images;

        void apply(const nlohmann::json& config);
    };

    struct SkinInfo {
        std::string id;
        std::string name;
    };

    class Skin {
        protected:
        Game* game;
        std::string id;
        std::vector<std::filesystem::path> chain;
        SkinManifest skin_manifest;

        std::array<std::optional<std::vector<SpriteDecal>>, SKIN_IMAGE_COUNT> images;
        std::array<std::optional<int32_t>, SKIN_SOUND_COUNT> sounds;
        std::optional<std::vector<int32_t>> menu_plucks;
        std::array<SpriteDecal, HIT_SCORE_COUNT> hit_score_decals;
        std::unique_ptr<olc::Sprite> title_gradient;

        void load();
        void unload();
        std::filesystem::path find(const std::string& file) const;
        std::optional<std::filesystem::path> find_sound(const std::string& stem) const;
        const std::vector<SpriteDecal>& lane_images(SkinImage image);

        public:
        static constexpr const char* DEFAULT_SKIN = "default";
        static constexpr const char* MANIFEST_FILE = "skin.json";
        static constexpr const char* DISPLAY_FONT_FILE = "display_font.ttf";
        static constexpr const char* GRADIENT_FILE = "title_gradient.png";
        static constexpr const char* SYMBOL_FONT_PATH = "assets/fonts/OpenArrow-Regular.ttf";
        static constexpr size_t MAX_INHERITANCE_DEPTH = 8;
        static constexpr size_t MAX_MENU_PLUCKS = 10;
        static constexpr uint16_t DISPLAY_FONT_LARGE_SIZE = 72;
        static constexpr uint16_t DISPLAY_FONT_MEDIUM_SIZE = 36;
        static constexpr uint16_t DISPLAY_FONT_SMALL_SIZE = 18;

        static std::filesystem::path builtin_skins_directory();
        static std::filesystem::path user_skins_directory();
        static std::filesystem::path skin_directory(const std::string& id);
        static std::vector<SkinInfo> available_skins();

        olc::Font display_font_large;
        olc::Font display_font_medium;
        olc::Font display_font_small;

        SpriteDecal game_paused_decal;

        Skin(Game* game, const std::string& id);
        ~Skin();
        Skin(const Skin&) = delete;
        Skin& operator=(const Skin&) = delete;

        void reload(const std::string& id);
        const std::string& get_id() const;
        const SkinManifest& manifest() const;

        olc::Decal* image(SkinImage image, uint8_t lane, uint8_t key_count);
        olc::Decal* hit_score_decal(HitScore score) const;
        olc::Pixel title_gradient_at(double x, double y) const;
        int32_t sound(SkinSound sound);
        int32_t get_random_menu_pluck();
    };
}
