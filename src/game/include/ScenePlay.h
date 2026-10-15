#pragma once

#include "gse/Scene.h"
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

        bool chart_started = false;
        bool paused = false;
        static inline bool show_hit_log = false;
        bool finished = false;
        bool audio_finished = false;

        void pause(double now);
        void resume(double now);
        void finish();
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
        double frame_percentile_ms(double fraction) const;
        std::vector<std::string> diagnostics_lines(bool full) const;
        void copy_diagnostics();
        nlohmann::json timing_diagnostics() const;
        void draw_timing_diagnostics(olc::PixelGameEngine* window) const;

        public:
        void on_resize() override;
        static constexpr float RESTART_HOLD_TIME = 0.3f;
        static constexpr double PAUSE_COOLDOWN_MS = 1000.0;
        static constexpr double TICKET_WAIT_LIMIT_MS = 10000.0;
        static constexpr double TICKET_HINT_DELAY_MS = 1000.0;
        static constexpr float TICKET_HINT_FADE_S = 0.3f;

        ScenePlay(Chart* chart, Skin* skin);
        ~ScenePlay();
        void on_create() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
