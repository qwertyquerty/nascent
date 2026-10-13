#pragma once

#include "ChartLibrary.h"
#include "ChartMenu.h"
#include "LeaderboardPanel.h"
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
#include <unordered_set>
#include <vector>

namespace nascent {
    class Chart;
    class DifficultyCache;
    class EntityField;
    class EntityStarfield;
    class Skin;

    class SceneList : public Scene {
        protected:
        struct BackgroundLoadRequest {
            uint64_t generation = 0;
            std::string image_path;
        };

        struct DifficultyJob {
            uint64_t generation = 0;
            size_t index = 0;
            const Chart* chart = nullptr;
            float pitch = 1.0f;
        };

        struct DifficultyResult {
            uint64_t generation = 0;
            size_t index = 0;
            float difficulty = 0;
        };

        struct BackgroundLoadResult {
            uint64_t generation = 0;
            std::unique_ptr<olc::Sprite> sprite;
        };

        std::unique_ptr<DifficultyCache> difficulty_cache;

        ChartLibrary library;
        std::vector<size_t> visible;
        std::string search;
        std::optional<std::string> pending_selection;
        std::filesystem::path pending_selection_path;
        int pending_audio_id = -1;
        std::unordered_set<std::string> local_hashes;
        Chart* selected_chart = nullptr;
        int32_t selected_chart_index = 0;

        std::unique_ptr<EntityField> field;

        int selected_chart_audio_id = -1;
        Chart* preview_audio_chart = nullptr;
        SongClock preview_clock;
        double preview_position = 0;

        float list_scroll_offset = 0;

        Skin* skin;
        EntityStarfield* sky;
        bool active = false;

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

        std::mutex difficulty_mutex;
        std::condition_variable difficulty_cv;
        std::vector<DifficultyJob> difficulty_jobs;
        std::vector<DifficultyResult> difficulty_results;
        std::vector<size_t> wanted_difficulties;
        uint64_t difficulty_generation = 0;
        bool difficulty_busy = false;
        bool difficulty_stopping = false;
        std::thread difficulty_worker;

        float widest_grade = -1.0f;

        std::optional<ChartMenu> chart_menu;
        LeaderboardPanel leaderboards;
        bool show_leaderboards = false;
        bool recommended_started = false;

        void start_library_load();
        void poll_library_load();
        void apply_library(ChartLibrary loaded);
        void apply_search(size_t keep_chart);
        void refresh_search();
        void update_search();
        void draw_search(olc::PixelGameEngine* window);
        void select_chart(int32_t, bool sound = true);
        void create_preview_field();
        void set_pitch(float);
        std::optional<float> get_difficulty(size_t index) const;
        void difficulty_loop();
        void request_difficulties();
        void cancel_difficulties(bool wait);
        void collect_difficulties();

        void request_background_load();
        void apply_background_load(std::unique_ptr<olc::Sprite> sprite);
        void background_load_loop();
        void stop_preview_audio();
        void draw_list(olc::PixelGameEngine* window);
        std::string selected_hash() const;
        olc::vi2d side_panel_position(olc::vi2d screen) const;
        olc::vi2d side_panel_size(olc::vi2d screen) const;
        void download_recommended();
        void draw_recommended(olc::PixelGameEngine* window);
        bool update_controls();
        void update_chart_menu();
        size_t pack_chart_count() const;
        void open_chart_website();
        void delete_charts(bool whole_pack);

        public:
        static constexpr float DIFFICULTY_BAR_MAX = 100.0f;
        static constexpr float DIFFICULTY_BAR_WIDTH = 300.0f;
        static constexpr float DIFFICULTY_BAR_HEIGHT = 6.0f;
        static constexpr float LENGTH_OFFSET = 90.0f;
        static constexpr float GRADE_SCREEN_MARGIN = 8.0f;
        static constexpr float GRADE_GAP = 8.0f;
        static constexpr int32_t LIST_ROW_HEIGHT = 100;
        static constexpr int32_t RECOMMENDED_ROW_HEIGHT = 30;
        static constexpr int32_t RECOMMENDED_STATUS_OFFSET = 540;
        static constexpr const char* DIFFICULTY_CACHE_FILE = "difficulty_cache.json";

        SceneList(Skin* skin, EntityStarfield* sky);
        ~SceneList();
        void init(Game*) override;
        void on_enter() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;

        const std::unordered_set<std::string>& get_local_hashes() const;
        void select_chart_by_hash(std::string hash, std::filesystem::path path, int playing_audio_id = -1);
    };
}
