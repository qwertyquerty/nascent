#pragma once

#include "Scene.h"
#include "ServerApi.h"
#include "olcPixelGameEngine.h"

#include <future>
#include <memory>
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

        int chart_audio_id = -1;
        std::shared_future<ApiResult<PlayTicket>> ticket;

        Skin* skin;
        Scene* return_scene;

        double timer = 0;
        double paused_position = 0;

        bool chart_started = false;
        bool paused = false;
        static inline bool show_hit_log = false;
        bool finished = false;
        bool audio_finished = false;

        void pause(double now);
        void resume(double now);
        void finish();

        public:
        ScenePlay(Chart* chart, Skin* skin, Scene* return_scene);
        ~ScenePlay();
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
