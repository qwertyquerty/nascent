#pragma once

#include "Scene.h"
#include "SongClock.h"
#include "StatusBar.h"
#include "Util.h"

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace nascent {
    class Chart;
    class EntityField;
    class Skin;

    class SceneList : public Scene {
        protected:
        struct BackgroundLoadRequest {
            uint64_t generation = 0;
            std::string image_path;
        };

        struct BackgroundLoadResult {
            uint64_t generation = 0;
            std::unique_ptr<olc::Sprite> sprite;
        };

        std::vector<std::unique_ptr<Chart>> charts;
        std::vector<std::optional<float>> chart_difficulties;
        Chart* selected_chart = nullptr;
        int32_t selected_chart_index = 0;

        std::unique_ptr<EntityField> field;

        int selected_chart_audio_id = -1;
        Chart* preview_audio_chart = nullptr;
        SongClock preview_clock;
        double preview_position = 0;

        const uint8_t list_chart_height = 100;

        float list_scroll_offset = 0;

        Skin* skin;

        StatusBar status_bar;

        SpriteDecal background_image;
        SpriteDecal old_background_image;

        uint64_t load_generation = 0;
        std::mutex background_load_mutex;
        std::condition_variable background_load_cv;
        bool background_load_stopping = false;
        std::optional<BackgroundLoadRequest> pending_background_load;
        std::optional<BackgroundLoadResult> completed_background_load;

        double timer = 0;

        bool crossfade = false;
        float crossfade_timer = 0;
        float audio_fadein_timer = 0;

        float pitch = 1.0;

        std::thread background_loader;

        void load_charts();
        void select_chart(int32_t);
        void set_pitch(float);
        std::optional<float> get_difficulty(size_t index, int32_t& budget);

        void request_background_load();
        void apply_background_load(std::unique_ptr<olc::Sprite> sprite);
        void background_load_loop();
        void stop_preview_audio();
        void draw_list(olc::PixelGameEngine* window);

        public:
        SceneList(Skin* skin);
        ~SceneList();
        void init(Game*) override;
        void on_enter() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
