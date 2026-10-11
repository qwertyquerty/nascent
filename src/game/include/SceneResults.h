#pragma once

#include "Results.h"
#include "Scene.h"
#include "ServerApi.h"
#include "Util.h"
#include "olcPixelGameEngine.h"

#include <future>
#include <optional>
#include <string>

namespace nascent {
    class Chart;
    class Skin;

    class SceneResults : public Scene {
        protected:
        Skin* skin;
        Scene* return_scene;
        const Chart* chart;
        AttemptSummary summary;
        std::string replay_message;
        std::future<ApiResult<Submission>> submission;
        std::optional<ApiResult<Submission>> submitted;
        int song_audio_id = -1;

        void release_song();

        SpriteDecal background_image;
        SpriteDecal error_graph;
        olc::vi2d error_graph_pos;

        void build_error_graph(olc::vi2d size);
        void draw_submission(int32_t x, int32_t y) const;

        public:
        SceneResults(Skin* skin, Scene* return_scene, const Chart* chart, AttemptSummary summary, std::string replay_message, std::future<ApiResult<Submission>> submission, int song_audio_id = -1);
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
