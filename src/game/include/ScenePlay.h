#pragma once

#include "Replay.h"
#include "gse/Scene.h"
#include "gse/render/MipDecal.h"
#include "gse/ui/Layout.h"
#include "ServerApi.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Chart;
    class EntityField;
    class PlaySession;
    class Skin;

    class ScenePlay : public Scene {
        friend class PauseOverlay;

        protected:
        Chart* chart;
        std::unique_ptr<PlaySession> session;
        std::unique_ptr<EntityField> field;
        std::vector<olc::Key> lane_keys;
        bool mirror = false;

        int chart_audio_id = -1;
        std::shared_future<ApiResult<PlayTicket>> ticket;
        double ticket_requested_at = 0;
        bool waiting_for_ticket = false;
        float ticket_hint_alpha = 0;

        Skin* skin;

        std::optional<Replay> replay;
        std::future<double> difficulty_load;
        std::optional<double> difficulty;
        std::string replay_label;
        size_t replay_cursor = 0;

        double timer = 0;
        double paused_position = 0;
        std::optional<double> resumed_at;
        float restart_hold = 0;
        uint32_t frames_played = 0;
        double frame_time_total = 0;
        static constexpr size_t FRAME_HISTOGRAM_BUCKETS = 1000;
        static constexpr double FRAME_HISTOGRAM_STEP_MS = 0.1;
        std::array<uint32_t, FRAME_HISTOGRAM_BUCKETS> frame_histogram = {};
        double worst_frame_ms = 0;
        std::optional<double> last_press_time;
        std::optional<double> closest_press_gap;
        uint32_t simultaneous_presses = 0;
        std::vector<std::array<double, 2>> clock_samples;
        std::vector<std::array<double, 3>> pauses;
        std::optional<std::array<double, 2>> open_pause;
        std::optional<double> clock_origin;

        bool chart_started = false;
        bool paused = false;
        bool replay_paused = false;
        mutable Hotspots replay_spots;

        MipDecal background_image;
        const MipDecal* borrowed_background = nullptr;
        std::future<std::vector<std::unique_ptr<olc::Sprite>>> background_load;
        float background_fade = 0;

        static inline bool show_hit_log = false;
        bool finished = false;
        bool audio_finished = false;

        void pause(double now);
        void resume(double now);
        void toggle_replay_pause(double now);
        void feed_replay(double position, bool sounds);
        void seek_replay(double target, double now);
        void finish();
        double chart_difficulty();
        void start_attempt();
        void request_ticket();
        bool ticket_pending(double now);
        uint8_t chart_lane(uint8_t lane) const;
        uint32_t chart_lanes(uint32_t lanes) const;
        void restart();
        bool can_skip_intro(double now) const;
        void skip_intro(double now);
        void record_frame(float elapsed_time);
        void record_press(double time);
        void record_clock_sample(double now);
        double wall_time(double now);
        double frame_percentile_ms(double fraction) const;
        std::vector<std::string> diagnostics_lines(bool full) const;
        void copy_diagnostics();
        nlohmann::json timing_diagnostics() const;
        void draw_timing_diagnostics(olc::PixelGameEngine* window) const;
        void draw_replay_bar(olc::PixelGameEngine* window) const;
        void load_background();
        void draw_background(olc::PixelGameEngine* window) const;

        public:
        void on_resize() override;
        static constexpr float RESTART_HOLD_TIME = 0.3f;
        static constexpr double PAUSE_COOLDOWN_MS = 1000.0;
        static constexpr double REPLAY_SEEK_MS = 5000.0;
        static constexpr int32_t REPLAY_TOGGLE = 0;
        static constexpr int32_t REPLAY_BACK = 1;
        static constexpr int32_t REPLAY_FORWARD = 2;
        static constexpr int32_t REPLAY_RESTART = 3;
        static constexpr int32_t REPLAY_MENU = 4;
        static constexpr int32_t REPLAY_STOP = 5;
        static constexpr float BACKGROUND_FADE_TIME = 0.4f;
        static constexpr double TICKET_WAIT_LIMIT_MS = 10000.0;
        static constexpr double CLOCK_SAMPLE_INTERVAL_MS = 1000.0;
        static constexpr double TICKET_HINT_DELAY_MS = 1000.0;
        static constexpr float TICKET_HINT_FADE_S = 0.3f;

        ScenePlay(Chart* chart, Skin* skin);
        ScenePlay(Chart* chart, Skin* skin, Replay replay, std::string replay_label);
        void use_background(const MipDecal* background);
        ~ScenePlay();
        void on_create() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
