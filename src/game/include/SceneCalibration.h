#pragma once

#include "gse/Scene.h"
#include "gse/ui/Layout.h"
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
        bool update_mouse(Input& input, bool coarse);

        Hotspots spots;

        public:
        SceneCalibration(Skin* skin);
        ~SceneCalibration();
        void on_create() override;

        static constexpr int32_t SPOT_STEP = 0;
        static constexpr int32_t SPOT_MINUS = 10;
        static constexpr int32_t SPOT_PLUS = 20;
        static constexpr int32_t SPOT_CLEAR_TAPS = 30;
        static constexpr int32_t SPOT_SAVE = 31;
        static constexpr int32_t VALUE_COLUMN = 380;
        static constexpr int32_t BUTTON_GAP = 28;
        static constexpr int32_t ROW_HEIGHT = 48;
        static constexpr float FOOTER_GAP = 48.0f;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
