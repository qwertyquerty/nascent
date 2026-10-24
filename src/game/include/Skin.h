#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"
#include "Accuracy.h"
#include "Const.h"
#include "Results.h"
#include "gse/ui/Theme.h"
#include "json.h"
#include "gse/render/MipDecal.h"
#include "gse/render/SpriteDecal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nascent {
    class Game;

    enum class SkinImage : uint8_t {
        NOTE,
        RECEPTOR,
        RECEPTOR_PRESSED,
        HOLD_HEAD,
        HOLD_BODY,
        HOLD_CAP,
        MASCOT,
        JUDGEMENT_PERFECT,
        JUDGEMENT_AMAZING,
        JUDGEMENT_GREAT,
        JUDGEMENT_GOOD,
        JUDGEMENT_BAD,
        JUDGEMENT_MISS,
        PLAY_BACKGROUND,
        MENU_BACKGROUND,
        LANE_BACKGROUND,
        LANE_LIGHT,
        STAGE_LEFT,
        STAGE_RIGHT,
        LANE_SEPARATOR,
        JUDGEMENT_LINE,
        LANE_COVER,
        HOLD_HEAD_HELD,
        HOLD_BODY_HELD,
        GRADE_X,
        GRADE_S,
        GRADE_A,
        GRADE_B,
        GRADE_C,
        GRADE_D,
        GRADE_E,
        GRADE_F,
        PROGRESS_TRACK,
        PROGRESS_FILL,
        TIMING_BAR_BACKGROUND,
        TIMING_BAR_HIT,
        TIMING_BAR_AVERAGE
    };

    inline constexpr size_t SKIN_IMAGE_COUNT = 37;

    inline constexpr std::array<const char*, 9> NOTE_SNAP_NAMES = {"4th", "8th", "12th", "16th", "24th", "32nd", "48th", "64th", "192nd"};
    inline constexpr std::array<uint8_t, 9> NOTE_SNAP_DIVISIONS = {1, 2, 3, 4, 6, 8, 12, 16, 48};
    inline constexpr size_t NOTE_SNAP_COUNT = NOTE_SNAP_NAMES.size();

    enum class SkinSound : uint8_t {
        HITSOUND,
        IDLE_LOOP,
        ACCOUNT_LOOP,
        JUDGEMENT_PERFECT,
        JUDGEMENT_AMAZING,
        JUDGEMENT_GREAT,
        JUDGEMENT_GOOD,
        JUDGEMENT_BAD,
        JUDGEMENT_MISS
    };

    inline constexpr size_t SKIN_SOUND_COUNT = 9;

    enum class SkinFont : uint8_t {
        LARGE,
        MEDIUM,
        SMALL,
        JUDGEMENT,
        COMBO
    };

    inline constexpr std::array<const char*, 5> SKIN_FONT_NAMES = {"large", "medium", "small", "judgement", "combo"};
    inline constexpr size_t SKIN_FONT_COUNT = SKIN_FONT_NAMES.size();

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
        TIMING_EARLY,
        TIMING_LATE,
        HOLD_DROPPED,
        SNAP_4TH,
        SNAP_8TH,
        SNAP_12TH,
        SNAP_16TH,
        SNAP_24TH,
        SNAP_32ND,
        SNAP_48TH,
        SNAP_64TH,
        SNAP_192ND,
    };

    inline constexpr std::array<const char*, 68> SKIN_COLOR_NAMES = {
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
        "timing_early",
        "timing_late",
        "hold_dropped",
        "snap_4th",
        "snap_8th",
        "snap_12th",
        "snap_16th",
        "snap_24th",
        "snap_32nd",
        "snap_48th",
        "snap_64th",
        "snap_192nd",
    };

    inline constexpr size_t SKIN_COLOR_COUNT = SKIN_COLOR_NAMES.size();
    static_assert((size_t)SkinColor::SNAP_192ND + 1 == SKIN_COLOR_COUNT);
    static_assert((size_t)SkinColor::SNAP_192ND - (size_t)SkinColor::SNAP_4TH + 1 == NOTE_SNAP_COUNT);

    struct SkinMascot {
        double width = 20;
        double x = 85;
        double y = 70;
    };

    struct SkinFontSpec {
        std::string file;
        double size = 0;
    };

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
        uint8_t receptor_pressed_alpha = 255;
        uint8_t note_alpha = 255;
        double frame_rate = 12;
        std::map<std::string, double, std::less<>> frame_rates;
        std::map<uint8_t, std::vector<uint8_t>> lane_images;
        std::map<uint8_t, nlohmann::json> keymodes;
        SkinMascot mascot;
        std::array<SkinFontSpec, SKIN_FONT_COUNT> fonts;
        std::array<std::optional<olc::Pixel>, SKIN_COLOR_COUNT> colors;
        std::vector<std::pair<double, olc::Pixel>> difficulty_scale;
        std::map<std::string, double, std::less<>> layout;
        std::vector<std::string> problems;

        void apply(const nlohmann::json& config);
        void apply_playfield(const nlohmann::json& config, const std::string& context);
        void apply_colors(const nlohmann::json& config);
        void apply_difficulty_scale(const nlohmann::json& config);
        void apply_fonts(const nlohmann::json& config);
        void apply_layout(const nlohmann::json& config, const std::string& prefix);
        void problem(std::string message);
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

        using ImageFrames = std::vector<std::filesystem::path>;
        using ImageLanes = std::vector<ImageFrames>;
        using ImageFiles = std::array<ImageLanes, SKIN_IMAGE_COUNT>;
        using DecalKey = std::pair<std::filesystem::path, bool>;
        using SnapFiles = std::array<std::array<ImageLanes, NOTE_SNAP_COUNT>, 2>;

        struct ResolvedImage {
            const ImageFrames* frames = nullptr;
            std::vector<const MipDecal*> decals;
            double fps = 0;
            bool clamp = true;
        };

        struct LayoutHash {
            using is_transparent = void;

            size_t operator()(std::string_view key) const {
                return std::hash<std::string_view>{}(key);
            }
        };

        using LayoutValues = std::unordered_map<std::string, double, LayoutHash, std::equal_to<>>;

        static constexpr int32_t UNRESOLVED_IMAGE = -1;
        static constexpr int32_t MISSING_IMAGE = -2;
        static constexpr size_t IMAGE_LOOKUP_SIZE = SKIN_IMAGE_COUNT * (NOTE_SNAP_COUNT + 1) * MAX_LANES;

        std::filesystem::path overlay;
        Skin* logo_skin = nullptr;
        std::map<uint8_t, ImageFiles> image_files;
        std::map<uint8_t, SnapFiles> snap_files;
        uint8_t layout_key_count = 0;
        std::map<DecalKey, MipDecal> decals;
        std::map<DecalKey, std::future<std::vector<std::unique_ptr<olc::Sprite>>>> pending_decals;
        mutable std::map<uint8_t, SkinManifest> keymode_manifests;
        mutable std::array<const SkinManifest*, MAX_LANES + 1> manifest_lookup = {};
        mutable std::array<std::optional<LayoutValues>, MAX_LANES + 1> layout_tables;
        mutable std::array<const LayoutValues*, MAX_LANES + 1> layout_lookup = {};
        std::array<std::vector<int32_t>, MAX_LANES + 1> image_lookup;
        std::vector<ResolvedImage> resolved_images;
        mutable std::set<std::string, std::less<>> missing_layout;
        std::vector<std::string> load_problems;
        std::array<std::optional<int32_t>, SKIN_SOUND_COUNT> sounds;
        std::array<std::optional<std::vector<int32_t>>, SKIN_SOUND_COUNT> voices;
        std::array<size_t, SKIN_SOUND_COUNT> next_voice = {};
        std::optional<std::vector<int32_t>> menu_plucks;
        std::array<SpriteDecal, HIT_SCORE_COUNT> hit_score_decals;
        std::unique_ptr<olc::Sprite> title_gradient;
        std::chrono::steady_clock::time_point loaded_at;

        double font_scale = 1.0;

        void load();
        void unload();
        void load_fonts();
        void unload_fonts();
        void check_files(const std::filesystem::path& directory);
        void sync_logo();
        std::filesystem::path find(const std::string& file) const;
        std::optional<std::filesystem::path> find_sound(const std::string& stem) const;
        ImageLanes image_paths(const std::string& stem, uint8_t key_count) const;
        std::optional<ResolvedImage> resolve_image(SkinImage image, uint8_t lane, uint8_t key_count, std::optional<uint8_t> snap) const;
        const SkinManifest& build_manifest(uint8_t key_count) const;
        const LayoutValues& layout_values(uint8_t key_count) const;
        double missing_layout_value(std::string_view key) const;
        void clear_lookups();
        void queue_decals(const ImageLanes& lanes, bool clamp);
        void start_image_loads();
        const MipDecal& decal(const std::filesystem::path& path, bool clamp);

        public:
        static constexpr const char* DEFAULT_SKIN = "default";
        static constexpr const char* LOGO_SKIN = "logo";
        static constexpr const char* LOGO_FOLDER = "logo";
        static constexpr const char* MANIFEST_FILE = "skin.json";
        static constexpr const char* DISPLAY_FONT_FILE = "display_font.ttf";
        static constexpr const char* GRADIENT_FILE = "title_gradient.png";
        static constexpr const char* SYMBOL_FONT_PATH = "assets/fonts/OpenArrow-Regular.ttf";
        static constexpr const char* CJK_FONT_PATH = "assets/fonts/MPLUS1-Regular.ttf";
        static constexpr size_t MAX_INHERITANCE_DEPTH = 8;
        static constexpr size_t MAX_MENU_PLUCKS = 10;
        static constexpr size_t MAX_ANIMATION_FRAMES = 256;
        static constexpr size_t HITSOUND_VOICES = 8;
        static constexpr std::array<uint16_t, SKIN_FONT_COUNT> DEFAULT_FONT_SIZES = {72, 36, 18, 72, 72};
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
        static uint32_t list_version();
        static void list_changed();

        olc::Font display_font_large;
        olc::Font display_font_medium;
        olc::Font display_font_small;
        olc::Font judgement_font;
        olc::Font combo_font;

        Skin(Game* game, const std::string& id);
        ~Skin();
        Skin(const Skin&) = delete;
        Skin& operator=(const Skin&) = delete;

        void reload(const std::string& id, std::filesystem::path overlay = {});
        void set_logo_skin(Skin* logo);
        void set_font_scale(double scale);
        const std::string& get_id() const;
        const SkinManifest& manifest() const;
        const SkinManifest& manifest(uint8_t key_count) const;
        const std::vector<std::string>& problems() const;
        double layout(std::string_view key) const override;
        double layout(std::string_view key, uint8_t key_count) const;
        void set_layout_key_count(uint8_t key_count);
        uint8_t get_layout_key_count() const;

        const MipDecal* image(SkinImage image, uint8_t lane, uint8_t key_count, std::optional<uint8_t> snap = std::nullopt, std::optional<double> since = std::nullopt);
        const MipDecal* judgement_image(HitScore score, uint8_t key_count = 0, std::optional<double> since = std::nullopt);
        const SkinMascot& mascot() const;
        olc::Decal* hit_score_decal(HitScore score) const;
        olc::Pixel title_gradient_at(double x, double y) const;
        olc::Pixel color(SkinColor color) const;
        olc::Font& font(SkinFont font);
        olc::Font& font(UiFont font) override;
        olc::Pixel color(UiColor color) const override;
        olc::Pixel accent(double position) const override;
        olc::Pixel judgement_color(HitScore score) const;
        olc::Pixel grade_color(Grade grade) const;
        olc::Pixel difficulty_color(double difficulty) const;
        olc::Pixel rank_color(int64_t rank) const;
        static std::array<olc::vf2d, GRADE_GLOW_STEPS> glow_offsets(float radius);
        static olc::Pixel glow_color(olc::Pixel color, float opacity);
        void draw_grade(Grade grade, olc::Font& font, olc::vf2d baseline, float scale = 1.0f, float opacity = 1.0f);
        void draw_judgement(HitScore score, olc::Font& font, olc::vf2d baseline, float opacity = 1.0f);
        int32_t sound(SkinSound sound);
        int32_t voice(SkinSound sound);
        int32_t hitsound_voice();
        int32_t judgement_voice(HitScore score);
        int32_t get_random_menu_pluck();
    };

    class SkinLayoutScope {
        Skin* skin;
        uint8_t previous;

        public:
        SkinLayoutScope(Skin* skin, uint8_t key_count) : skin(skin), previous(skin->get_layout_key_count()) {
            skin->set_layout_key_count(key_count);
        }

        ~SkinLayoutScope() {
            skin->set_layout_key_count(previous);
        }

        SkinLayoutScope(const SkinLayoutScope&) = delete;
        SkinLayoutScope& operator=(const SkinLayoutScope&) = delete;
    };
}
