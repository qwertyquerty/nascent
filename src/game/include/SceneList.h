#pragma once

#include "Scene.h"
#include "SongClock.h"
#include "StatusBar.h"
#include "Util.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nascent {
    class Chart;
    class EntityField;
    class Skin;
    struct SearchFilter;

    struct ChartLibrary {
        std::vector<std::unique_ptr<Chart>> charts;
        std::vector<std::string> hashes;
        std::vector<std::optional<float>> difficulties;
        std::vector<std::string> search_texts;
        std::unordered_map<std::string, std::string> hash_by_replay_folder;
        float pitch = 1.0f;
        bool hashed_files = false;
    };

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
        std::vector<std::string> chart_hashes;
        std::vector<std::string> search_texts;
        std::vector<size_t> visible;
        std::string search;
        std::optional<std::string> pending_selection;
        std::filesystem::path pending_selection_path;
        int pending_audio_id = -1;
        std::unordered_set<std::string> local_hashes;
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

        std::mutex library_load_mutex;
        std::optional<ChartLibrary> completed_library_load;
        std::thread library_loader;
        bool library_loading = false;
        bool library_reload_pending = false;

        void start_library_load();
        void poll_library_load();
        void apply_library(ChartLibrary library);
        void apply_search(size_t keep_chart);
        void refresh_search();
        bool matches_filter(size_t index, const SearchFilter& filter);
        void update_search();
        void draw_search(olc::PixelGameEngine* window);
        void select_chart(int32_t, bool sound = true);
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

        const std::unordered_set<std::string>& get_local_hashes() const;
        void select_chart_by_hash(std::string hash, std::filesystem::path path, int playing_audio_id = -1);
    };
}
