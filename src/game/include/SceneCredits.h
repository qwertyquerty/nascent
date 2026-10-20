#pragma once

#include "gse/Scene.h"
#include "gse/render/MipDecal.h"
#include "gse/ui/Layout.h"
#include "json.h"
#include "olcPixelGameEngine.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nascent {
    class Skin;

    struct CreditsSection {
        std::string heading;
        std::vector<std::string> names;
    };

    std::vector<CreditsSection> parse_credits(const nlohmann::json& credits);

    class SceneCredits : public Scene {
        protected:
        struct Arrow {
            float x;
            float y;
            float size;
            float speed;
            float hue;
            size_t image;
        };

        Skin* skin;
        std::vector<CreditsSection> sections;
        std::vector<Arrow> arrows;
        MipDecal logo;
        std::vector<MipDecal> arrow_images;
        Hotspots spots;
        float scroll = 0;
        float content_height = 0;
        bool scroll_started = false;

        void update_arrows(float elapsed_time, olc::vi2d screen);
        void draw_backdrop(olc::PixelGameEngine* window, olc::vi2d screen) const;
        std::optional<float> line_opacity(float baseline, olc::vi2d screen) const;
        void draw_line(olc::Font& font, const std::string& text, float baseline, olc::vi2d screen, olc::Pixel color, bool glow) const;
        void draw_logo(olc::PixelGameEngine* window, float baseline, olc::vi2d screen) const;

        public:
        static constexpr const char* CREDITS_PATH = "assets/credits.json";
        static constexpr const char* LOGO_PATH = "assets/logo/logo.png";
        static constexpr size_t MAX_ARROW_IMAGES = 32;
        static constexpr const char* TITLE = "Nascent";
        static constexpr const char* CLOSING = "Thanks for playing";
        static constexpr const char* BACK_HINT = "Esc  back";
        static constexpr int32_t ARROW_COUNT = 44;
        static constexpr uint32_t ARROW_SEED = 20261019;

        explicit SceneCredits(Skin* skin);
        void on_create() override;
        void on_resize() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
    };
}
