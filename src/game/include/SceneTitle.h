#pragma once

#include "gse/Scene.h"
#include "gse/ui/Layout.h"
#include "SongClock.h"
#include "Util.h"
#include "gse/render/SpriteDecal.h"

#include <cstdint>
#include <filesystem>
#include <future>
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

    struct TitleSong {
        std::unique_ptr<Chart> chart;
        PreparedSongAudio audio;
    };

    class SceneTitle : public Scene {
        protected:
        std::optional<TitleTrack> handoff;
        std::optional<std::future<std::shared_ptr<TitleSong>>> next_song;
        std::optional<std::vector<std::filesystem::path>> song_candidates;
        std::optional<std::vector<std::filesystem::path>> favorite_candidates;

        std::unique_ptr<Chart> logo_chart;
        std::unique_ptr<Attempt> logo_attempt;
        std::unique_ptr<EntityField> logo_field;
        SongClock logo_clock;
        double logo_position = 0;

        std::unique_ptr<Chart> bg_chart;
        size_t logo_note_cursor = 0;
        double logo_light_position = 0;
        std::vector<double> letter_hit_time;
        std::vector<double> letter_hold_end;
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

        static std::unique_ptr<Chart> load_random_chart(std::vector<std::filesystem::path> candidates);
        bool load_background();
        void apply_next_song();
        void update_loading(float elapsed_time);
        void draw_loading(olc::PixelGameEngine* window);
        void draw_cover(olc::PixelGameEngine* window, float opacity);
        float reveal_time() const;
        float bar_opacity() const;
        float cover_opacity() const;
        bool adopt_track(const TitleTrack& track);
        void build_background(std::unique_ptr<Chart> chart);
        void update_logo_lights();
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
        static constexpr const char* START_HINT = "PRESS ENTER TO START";
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
