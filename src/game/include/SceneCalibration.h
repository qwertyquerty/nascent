#pragma once

#include "Scene.h"
#include "SongClock.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class Chart;
    class EntityField;
    class Skin;

    class SceneCalibration : public Scene {
        protected:
        enum class Step {
            VISUAL_OFFSET,
            INPUT_OFFSET,
            SCROLL_SPEED
        };

        Skin* skin;
        Scene* return_scene;

        std::unique_ptr<Chart> chart;
        std::unique_ptr<EntityField> field;
        SongClock clock;
        int click_audio_id = -1;

        std::vector<olc::Key> lane_keys;
        std::vector<olc::Key> tap_keys;

        Step step = Step::VISUAL_OFFSET;
        int32_t visual_offset = 0;
        int32_t input_offset = 0;
        int32_t scroll_speed = 0;

        std::deque<double> tap_errors;
        std::string status_message;

        void adjust(int32_t direction, bool coarse);
        void change_step(int32_t direction);
        void record_tap(double event_time);
        double get_average_tap_error();
        void finish();

        public:
        SceneCalibration(Skin* skin, Scene* return_scene);
        ~SceneCalibration();
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
