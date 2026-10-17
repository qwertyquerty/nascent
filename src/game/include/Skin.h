#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"
#include "Accuracy.h"
#include "Results.h"
#include "gse/ui/Theme.h"
#include "json.h"
#include "gse/render/MipDecal.h"
#include "gse/render/SpriteDecal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
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
        STRETCH,
        TILE
    };

    enum class SkinColor : uint8_t {
        SCREEN_BACKGROUND,
        TEXT,
        TEXT_MUTED,
        TEXT_FAINT,
        TEXT_ERROR,
        POSITIVE,
        NEGATIVE,
        ATTENTION,
        FULL_COMBO,
        COMBO,
        STATUS_BAR_BACKGROUND,
        STATUS_BAR_EDGE,
        STATUS_IDLE,
        PANEL_BACKGROUND,
        SELECTION_HIGHLIGHT,
        MENU_BACKGROUND,
        MENU_DIM,
        MENU_DANGER_IDLE,
        TOAST_BACKGROUND,
        PROGRESS_TRACK,
        INPUT_BACKGROUND,
        INPUT_BACKGROUND_FOCUSED,
        SEPARATOR,
        SCROLLBAR_TRACK,
        SCROLLBAR_THUMB,
        OPTIONS_MASK,
        INPUT_LANE_BACKGROUND,
        DIAGNOSTICS_BACKGROUND,
        PAUSE_OVERLAY,
        PLAY_HINT,
        UPDATE_NOTICE,
        UPDATE_NOTICE_ERROR,
        BACKGROUND_TINT,
        GRAPH_BACKGROUND,
        GRAPH_CENTER_LINE,
        TIMING_BAR_HIT,
        BROWSER_HIGHLIGHT,
        DOWNLOAD_TRACK,
        SHOOTING_STAR_GLOW,
        JUDGEMENT_PERFECT,
        JUDGEMENT_AMAZING,
        JUDGEMENT_GREAT,
        JUDGEMENT_GOOD,
        JUDGEMENT_BAD,
        JUDGEMENT_MISS,
        GRADE_X,
        GRADE_S,
        GRADE_A,
        GRADE_B,
        GRADE_C,
        GRADE_D,
        GRADE_E,
        GRADE_F,
        RANK_FIRST,
        RANK_SECOND,
        RANK_THIRD,
    };

    inline constexpr std::array<const char*, 56> SKIN_COLOR_NAMES = {
        "screen_background",
        "text",
        "text_muted",
        "text_faint",
        "text_error",
        "positive",
        "negative",
        "attention",
        "full_combo",
        "combo",
        "status_bar_background",
        "status_bar_edge",
        "status_idle",
        "panel_background",
        "selection_highlight",
        "menu_background",
        "menu_dim",
        "menu_danger_idle",
        "toast_background",
        "progress_track",
        "input_background",
        "input_background_focused",
        "separator",
        "scrollbar_track",
        "scrollbar_thumb",
        "options_mask",
        "input_lane_background",
        "diagnostics_background",
        "pause_overlay",
        "play_hint",
        "update_notice",
        "update_notice_error",
        "background_tint",
        "graph_background",
        "graph_center_line",
        "timing_bar_hit",
        "browser_highlight",
        "download_track",
        "shooting_star_glow",
        "judgement_perfect",
        "judgement_amazing",
        "judgement_great",
        "judgement_good",
        "judgement_bad",
        "judgement_miss",
        "grade_x",
        "grade_s",
        "grade_a",
        "grade_b",
        "grade_c",
        "grade_d",
        "grade_e",
        "grade_f",
        "rank_1",
        "rank_2",
        "rank_3",
    };

    inline constexpr size_t SKIN_COLOR_COUNT = SKIN_COLOR_NAMES.size();
    static_assert((size_t)SkinColor::RANK_THIRD + 1 == SKIN_COLOR_COUNT);

    struct SkinManifest {
        std::string name;
        std::string inherits;
        bool hidden = false;
        SkinTint tint = SkinTint::GRADIENT;
        HoldStyle hold_style = HoldStyle::REPEAT;
        double key_spacing = 0.15;
        uint8_t hold_alpha = 64;
        std::optional<double> hold_body_aspect;
        double hold_body_head_offset = 0;
        double hold_body_tail_offset = 0;
        std::optional<uint8_t> receptor_alpha;
        std::map<uint8_t, std::vector<uint8_t>> lane_images;
        std::array<std::optional<olc::Pixel>, SKIN_COLOR_COUNT> colors;
        std::vector<std::pair<double, olc::Pixel>> difficulty_scale;

        void apply(const nlohmann::json& config);
        void apply_colors(const nlohmann::json& config);
        void apply_difficulty_scale(const nlohmann::json& config);
    };

    struct SkinInfo {
        std::string id;
        std::string name;
    };

    class Skin : public UiTheme {
        protected:
        Game* game;
        std::string id;
        std::vector<std::filesystem::path> chain;
        SkinManifest skin_manifest;

        std::array<std::optional<std::vector<MipDecal>>, SKIN_IMAGE_COUNT> images;
        std::array<std::vector<std::future<std::vector<std::unique_ptr<olc::Sprite>>>>, SKIN_IMAGE_COUNT> pending_images;
        std::array<std::optional<int32_t>, SKIN_SOUND_COUNT> sounds;
        std::optional<std::vector<int32_t>> menu_plucks;
        std::optional<std::vector<int32_t>> hitsound_voices;
        size_t next_hitsound_voice = 0;
        std::array<SpriteDecal, HIT_SCORE_COUNT> hit_score_decals;
        std::unique_ptr<olc::Sprite> title_gradient;

        double font_scale = 1.0;

        void load();
        void unload();
        void load_fonts();
        void unload_fonts();
        std::filesystem::path find(const std::string& file) const;
        std::optional<std::filesystem::path> find_sound(const std::string& stem) const;
        std::vector<std::filesystem::path> image_paths(SkinImage image) const;
        void start_image_loads();
        const std::vector<MipDecal>& lane_images(SkinImage image);

        public:
        static constexpr const char* DEFAULT_SKIN = "default";
        static constexpr const char* MANIFEST_FILE = "skin.json";
        static constexpr const char* DISPLAY_FONT_FILE = "display_font.ttf";
        static constexpr const char* GRADIENT_FILE = "title_gradient.png";
        static constexpr const char* SYMBOL_FONT_PATH = "assets/fonts/OpenArrow-Regular.ttf";
        static constexpr size_t MAX_INHERITANCE_DEPTH = 8;
        static constexpr size_t MAX_MENU_PLUCKS = 10;
        static constexpr size_t HITSOUND_VOICES = 8;
        static constexpr uint16_t DISPLAY_FONT_LARGE_SIZE = 72;
        static constexpr uint16_t DISPLAY_FONT_MEDIUM_SIZE = 36;
        static constexpr uint16_t DISPLAY_FONT_SMALL_SIZE = 18;
        static constexpr double DIFFICULTY_GRADIENT_MAX = 100.0;
        static constexpr double GRADE_GLOW_CYCLE = 0.25;
        static constexpr float GRADE_GLOW_RADIUS = 3.0f;
        static constexpr double GRADE_GLOW_ALPHA = 0.16;
        static constexpr int GRADE_GLOW_STEPS = 8;
        static constexpr double GRADE_SHIMMER_SPEED = 3.0;

        static std::filesystem::path builtin_skins_directory();
        static std::filesystem::path user_skins_directory();
        static std::filesystem::path skin_directory(const std::string& id);
        static std::vector<SkinInfo> available_skins();

        olc::Font display_font_large;
        olc::Font display_font_medium;
        olc::Font display_font_small;

        Skin(Game* game, const std::string& id);
        ~Skin();
        Skin(const Skin&) = delete;
        Skin& operator=(const Skin&) = delete;

        void reload(const std::string& id);
        void set_font_scale(double scale);
        const std::string& get_id() const;
        const SkinManifest& manifest() const;

        const MipDecal* image(SkinImage image, uint8_t lane, uint8_t key_count);
        olc::Decal* hit_score_decal(HitScore score) const;
        olc::Pixel title_gradient_at(double x, double y) const;
        olc::Pixel color(SkinColor color) const;
        olc::Font& font(UiFont font) override;
        olc::Pixel color(UiColor color) const override;
        olc::Pixel accent(double position) const override;
        olc::Pixel judgement_color(HitScore score) const;
        olc::Pixel grade_color(Grade grade) const;
        olc::Pixel difficulty_color(double difficulty) const;
        olc::Pixel rank_color(int64_t rank) const;
        static std::array<olc::vf2d, GRADE_GLOW_STEPS> glow_offsets(float radius);
        static olc::Pixel glow_color(olc::Pixel color, float opacity);
        void draw_grade(Grade grade, olc::Font& font, olc::vf2d baseline, float scale = 1.0f, float opacity = 1.0f) const;
        void draw_judgement(HitScore score, olc::Font& font, olc::vf2d baseline, float opacity = 1.0f) const;
        int32_t sound(SkinSound sound);
        int32_t hitsound_voice();
        int32_t get_random_menu_pluck();
    };
}
