#pragma once

#include "Results.h"
#include "gse/Scene.h"
#include "ServerApi.h"
#include "gse/render/MipDecal.h"
#include "olcPixelGameEngine.h"
#include "gse/render/SpriteDecal.h"

#include <future>
#include <optional>
#include <string>

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

        void release_song();

        MipDecal background_image;
        SpriteDecal error_graph;
        olc::vi2d error_graph_pos;

        void build_error_graph(olc::vi2d size);
        void layout_error_graph();
        void draw_bottom_bar(olc::PixelGameEngine* window) const;

        public:
        SceneResults(Skin* skin, const Chart* chart, AttemptSummary summary, std::future<ApiResult<Submission>> submission, int song_audio_id = -1);
        void on_create() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
