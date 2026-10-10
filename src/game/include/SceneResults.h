#pragma once

#include "Results.h"
#include "Scene.h"
#include "Util.h"
#include "olcPixelGameEngine.h"

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

        SpriteDecal error_graph;
        olc::vi2d error_graph_pos;

        void build_error_graph(olc::vi2d size);

        public:
        SceneResults(Skin* skin, Scene* return_scene, const Chart* chart, AttemptSummary summary, std::string replay_message);
        void init(Game*) override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
