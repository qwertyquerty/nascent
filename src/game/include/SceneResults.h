#pragma once

#include "Replay.h"
#include "Results.h"
#include "gse/Scene.h"
#include "ServerApi.h"
#include "gse/fx/ParticleEmitter.h"
#include "gse/ui/Layout.h"
#include "gse/render/MipDecal.h"
#include "olcPixelGameEngine.h"
#include "gse/render/SpriteDecal.h"

#include <future>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Chart;
    class Skin;

    class SceneResults : public Scene {
        protected:
        Skin* skin;
        const Chart* chart;
        AttemptSummary summary;
        std::future<ApiResult<Submission>> submission;
        std::optional<ApiResult<Submission>> submitted;
        int song_audio_id = -1;
        bool new_best = false;
        std::string replay_label;
        Chart* replay_chart = nullptr;
        std::optional<Replay> replay;
        std::string watch_label;
        const MipDecal* replay_background = nullptr;
        std::optional<int64_t> server_play_id;

        ParticleEmitter particles;
        mutable Hotspots spots;
        float elapsed = 0;
        bool slammed = false;

        void release_song();
        void burst(olc::vf2d center);
        olc::vf2d grade_baseline(olc::vi2d screen) const;

        MipDecal background_image;
        SpriteDecal error_graph;
        olc::vi2d error_graph_pos;
        olc::vi2d error_graph_size = {0, 0};
        double error_graph_scale = 0;

        void build_error_graph(olc::vi2d size);
        void layout_error_graph();
        void draw_bottom_bar(olc::PixelGameEngine* window) const;
        std::optional<int64_t> submitted_play_id() const;
        void open_play_page();
        void watch_replay();
        void retry();

        static constexpr int32_t SPOT_BACK = 0;
        static constexpr int32_t SPOT_PLAY_PAGE = 1;
        static constexpr int32_t SPOT_WATCH = 2;
        static constexpr int32_t SPOT_RETRY = 3;

        public:
        SceneResults(Skin* skin, const Chart* chart, AttemptSummary summary, std::future<ApiResult<Submission>> submission, int song_audio_id = -1, bool new_best = false, std::string replay_label = {});
        void enable_replay(Chart* chart, Replay replay, std::string label, const MipDecal* background);
        void set_play_id(int64_t play_id);
        void on_create() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
