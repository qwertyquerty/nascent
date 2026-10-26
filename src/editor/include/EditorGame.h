#pragma once

#include "ChartDocument.h"
#include "EditorField.h"
#include "FileDialog.h"
#include "ImGuiBackend.h"
#include "OsuExport.h"
#include "PatternGenerator.h"
#include "Playback.h"
#include "Waveform.h"
#include "gse/Game.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nascent {
    class Skin;

    struct DifficultyResult {
        double rating = 0;
        std::string error;
    };

    class EditorGame : public Game {
        protected:
        enum class DialogPurpose {
            NONE,
            OPEN_CHART,
            NEW_FROM_AUDIO,
            CHOOSE_AUDIO,
            CHOOSE_IMAGE,
            SAVE_AS,
            EXPORT_OSU,
            EXPORT_STEPMANIA,
            EXPORT_OSZ,
            EXPORT_ZIP,
        };

        enum class Pending {
            NONE,
            NEW_FROM_AUDIO,
            OPEN_CHART,
            QUIT,
        };

        struct Gesture {
            enum class Kind {
                NONE,
                PLACE,
                MOVE,
                SELECT,
            };

            Kind kind = Kind::NONE;
            uint8_t key = 0;
            uint8_t end_key = 0;
            double start = 0;
            double end = 0;
            double grab_offset = 0;
            bool moved = false;
            bool additive = false;
            Hit grabbed;
            std::vector<Hit> originals;
            std::vector<Hit> base;
            int32_t time_offset = 0;
            int32_t key_offset = 0;
        };

        struct GridLine {
            float y = 0;
            olc::Pixel color;
            float height = 1;
            int32_t measure = -1;
        };

        struct TimingMarker {
            float y = 0;
            std::string label;
        };

        struct Rect {
            float x = 0;
            float y = 0;
            float w = 0;
            float h = 0;
        };

        ImGuiBackend imgui;
        std::unique_ptr<Skin> skin;
        std::unique_ptr<ChartDocument> document;
        std::unique_ptr<Chart> preview;
        std::unique_ptr<EditorField> field;
        uint64_t preview_revision = ~0ull;
        Playback playback;
        FileDialog dialog;
        DialogPurpose dialog_purpose = DialogPurpose::NONE;
        Pending pending = Pending::NONE;
        std::filesystem::path pending_file;
        std::vector<ChartDocument::ChartChoice> chart_choices;
        std::filesystem::path choice_source;
        bool choice_requested = false;
        bool confirm_requested = false;
        bool quit_confirmed = false;
        bool update_restart_requested = false;
        std::unordered_map<unsigned int, int> editing_ints;

        Rect view;
        double position = 0;
        uint32_t divisor = 4;
        bool snap_enabled = true;
        uint32_t scroll_speed = 160;
        float judge_offset = 0.2f;
        bool hitsounds = true;
        bool metronome = false;
        bool snap_colors = true;
        uint8_t music_volume = 80;
        uint8_t hitsound_volume = 60;
        uint8_t metronome_volume = 70;
        OsuDifficulty osu_difficulty;
        bool osu_prompt_requested = false;
        bool audio_prompt_requested = false;
        bool transform_requested = false;
        bool quantize_requested = false;
        uint32_t quantize_divisor = 4;
        bool quantize_ends = true;
        bool pattern_open = false;
        size_t pattern_style = 0;
        size_t pattern_variant = 0;
        uint32_t pattern_divisor = 4;
        int pattern_beats = 8;
        PatternSettings pattern_settings;
        bool pattern_replace = true;
        uint32_t pattern_seed = 1;
        std::vector<Hit> pattern_preview;
        bool transform_from_playhead = false;
        int rotate_columns = 1;
        std::filesystem::path dropped_audio;
        DialogPurpose osu_prompt_purpose = DialogPurpose::NONE;
        float waveform_opacity = 0.2f;
        std::optional<float> note_opacity;
        std::optional<int> lane_width;
        double hitsound_position = 0;
        std::array<int, 2> metronome_sounds = {-1, -1};
        Waveform waveform;
        bool layout_ready = false;

        Gesture gesture;
        std::vector<GridLine> grid;
        std::vector<TimingMarker> markers;
        std::vector<float> density;
        uint64_t density_revision = ~0ull;
        double density_length = 0;
        float density_peak = 0;
        bool show_ghost = false;
        std::vector<Hit> selection;
        std::vector<Hit> clipboard;
        uint64_t selection_revision = ~0ull;
        std::optional<uint8_t> hover_column;
        double hover_time = 0;

        std::future<DifficultyResult> difficulty_job;
        uint64_t difficulty_revision = ~0ull;
        uint64_t changed_revision = ~0ull;
        double changed_at = 0;
        std::optional<DifficultyResult> difficulty;

        std::string status;
        bool status_error = false;
        double status_at = 0;

        std::filesystem::path settings_directory;
        std::vector<std::filesystem::path> recent_files;
        std::optional<size_t> shown_timing_point;
        std::vector<double> taps;
        bool taps_from_song = false;
        double last_tap_at = 0;
        bool round_tapped_bpm = false;

        void on_screen_resize() override;
        void set_status(std::string message, bool error = false);
        void load_skin(const std::string& id);
        void replace_document(std::unique_ptr<ChartDocument> next);
        void reload_audio();
        void open_chart(const std::filesystem::path& path);
        void open_choice(size_t index);
        void load_recent();
        void save_recent() const;
        void remember(const std::filesystem::path& path);
        void forget_recent(const std::filesystem::path& path);
        std::optional<std::filesystem::path> draw_recent_items();
        void new_from_audio(const std::filesystem::path& audio);
        void save();
        void save_as();
        void request(Pending action);
        void perform(Pending action);
        void seek(double target);
        void step(int32_t steps);
        void toggle_playback();
        void change_snap(int32_t direction);
        void change_scroll_speed(int32_t notches);
        int lane_width_percent() const;
        void change_lane_width(int32_t steps);
        double place_time(double time) const;
        std::string snap_label() const;
        void update_preview();
        void update_difficulty();
        void play_hitsounds(double from, double to);
        void play_metronome(double from, double to);
        void play_sound(int id, uint8_t volume);
        void load_metronome();
        void open_export(DialogPurpose purpose, const char* title, const char* extension);
        void finish_export(DialogPurpose purpose, const std::filesystem::path& path);

        void build_layout(unsigned int dockspace);
        void draw_menu();
        void draw_transport();
        void draw_chart_panel();
        void draw_timing_panel();
        void tap_tempo();
        void draw_tap_tempo();
        void draw_help_panel();
        void draw_update_menu();
        void update_updater();
        void draw_status_bar();
        void draw_dialogs();
        void handle_dropped_files();
        void handle_shortcuts();
        void handle_field_input();
        bool is_selected(const Hit& hit) const;
        void prune_selection();
        std::pair<int32_t, int32_t> clamp_offsets(const std::vector<Hit>& hits, int32_t time_offset, int32_t key_offset) const;
        void move_selection(int32_t time_offset, int32_t key_offset);
        void nudge_selection(int32_t steps, int32_t lanes);
        void mirror_selection();
        void rotate_selection(int32_t columns);
        void stretch_selection(double factor, bool from_playhead);
        void draw_transform_popup();
        void draw_quantize_popup();
        static bool snap_picker(const char* label, uint32_t& value);
        std::vector<Hit> pattern_hits() const;
        void draw_pattern_window();
        void draw_pattern_settings(PatternFamily family);
        void place_pattern();
        void select_all();
        void copy_selection();
        void cut_selection();
        void paste_clipboard();
        void delete_selection();
        void layout_field();
        void collect_grid();
        double timeline_length() const;
        void update_density(size_t rows, double length);
        void draw_density_bar();
        void configure_field();
        void draw_underlay(olc::PixelGameEngine* window);
        void draw_waveform(olc::PixelGameEngine* window);
        void draw_overlays(olc::PixelGameEngine* window);

        public:
        static constexpr uint8_t EDITOR_LAYER = 1;
        static constexpr std::array<uint32_t, 11> DIVISORS = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48};
        static constexpr std::array<uint32_t, 8> COMPLEX_TUPLET_DIVISORS = {5, 7, 9, 10, 11, 13, 14, 15};
        static constexpr int MAX_DIVISOR = 96;
        static constexpr int MAX_PATTERN_BEATS = 128;
        static constexpr size_t MAX_PATTERN_ROWS = 4096;
        int custom_divisor = 20;
        static constexpr std::array<float, 7> RATES = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f};

        explicit EditorGame(olc::PixelGameEngine* window);
        ~EditorGame() override;

        void init() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        void render_interface();
        bool can_close();
    };
}
