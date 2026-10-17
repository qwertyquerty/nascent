#pragma once

#include "gse/Scene.h"
#include "gse/ui/Layout.h"
#include "SongClock.h"
#include "gse/render/SpriteDecal.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace nascent {
    class Attempt;
    class Chart;
    class EntityFFT;
    class EntityField;
    class EntityStarfield;
    class SceneList;
    class Skin;

    struct TitleTrack {
        std::filesystem::path chart_path;
        int audio_id = -1;
    };

    class SceneTitle : public Scene {
        protected:
        std::optional<TitleTrack> handoff;

        std::unique_ptr<Chart> logo_chart;
        std::unique_ptr<Attempt> logo_attempt;
        std::unique_ptr<EntityField> logo_field;
        SongClock logo_clock;
        double logo_position = 0;

        std::unique_ptr<Chart> bg_chart;
        std::vector<std::unique_ptr<EntityField>> bg_field;
        SongClock bg_clock;
        double bg_position = 0;

        std::unique_ptr<EntityFFT> fft_l;
        std::unique_ptr<EntityFFT> fft_r;

        std::vector<float> audio_buffer;
        SpriteDecal start_hint;

        int logo_audio_id = -1;
        int bg_audio_id = -1;

        Skin* logo_skin;
        Skin* bg_skin;
        SceneList* list;
        EntityStarfield* sky;

        double timer = 0;
        float bg_fade = 0;

        bool loaded = false;
        bool logo_started = false;
        bool bg_started = false;
        bool waiting_for_library = false;
        bool loading = false;
        int32_t warmup_frames = 0;
        float shown_progress = 0;
        float reveal = 0;

        uint8_t get_field_count();
        std::unique_ptr<Chart> load_random_chart(std::vector<std::filesystem::path> candidates);
        bool load_background();
        void update_loading(float elapsed_time);
        void draw_loading(olc::PixelGameEngine* window);
        void draw_cover(olc::PixelGameEngine* window, float opacity);
        float bar_opacity() const;
        float cover_opacity() const;
        bool adopt_track(const TitleTrack& track);
        void build_background(std::unique_ptr<Chart> chart);
        void layout_background();
        bool logo_playing();
        bool logo_finished();
        void draw_update_notice(olc::PixelGameEngine* window);

        Hotspots spots;

        public:
        static constexpr const char* LOGO_CHART_PATH = "assets/logo/logo.json";
        static constexpr bool PLAY_LOGO_INTRO = false;
        static constexpr int32_t LOGO_FINISHED_OFFSET_MS = 1000;
        static constexpr float LOGO_FADE_DELAY = 0.5f;
        static constexpr float LOGO_FADE_IN_TIME = 0.5f;
        static constexpr float LOGO_HOLD_TIME = 2.0f;
        static constexpr float MAX_TIMER_STEP = 1.0f / 30.0f;
        static constexpr int32_t RANDOM_CHART_ATTEMPTS = 10;
        static constexpr int32_t WARMUP_FRAMES = 3;
        static constexpr float LOADING_BAR_WIDTH = 480.0f;
        static constexpr float LOADING_BAR_HEIGHT = 6.0f;
        static constexpr float LOADING_BAR_SPEED = 10.0f;
        static constexpr float LOADING_TRACK_OPACITY = 0.12f;
        static constexpr float LOADING_FILL_OPACITY = 0.35f;
        static constexpr float LOADING_BAR_FADE_TIME = 0.25f;
        static constexpr float STARFIELD_FADE_IN_TIME = 0.5f;
        static constexpr float BACKGROUND_FADE_IN_TIME = 1.0f;
        static constexpr float BACKGROUND_DIM = 0.5f;
        static constexpr const char* START_HINT = "PRESS ENTER TO START";
        static constexpr int32_t START_HINT_GAP = 24;
        static constexpr float START_HINT_PULSE_SPEED = 2.5f;
        static constexpr int32_t UPDATE_NOTICE_BOTTOM = 48;
        static constexpr int32_t SPOT_UPDATE = 0;

        SceneTitle(Skin* bg_skin, Skin* logo_skin, SceneList* list, EntityStarfield* sky, std::optional<TitleTrack> handoff = std::nullopt);
        ~SceneTitle();
        void on_create() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
