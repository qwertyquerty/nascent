#pragma once

#include "ChartLibrary.h"
#include "LibraryCache.h"
#include "ChartMenu.h"
#include "LeaderboardPanel.h"
#include "ReplayMenu.h"
#include "SortMenu.h"
#include "gse/Scene.h"
#include "SongClock.h"
#include "StatusBar.h"
#include "Util.h"
#include "gse/render/MipDecal.h"
#include "gse/render/ShapeSprite.h"
#include "gse/render/SpriteDecal.h"
#include "gse/ui/KeyRepeat.h"
#include "gse/ui/ScrollBar.h"

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
            std::filesystem::path audio_path;
            bool keep_image = false;
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

        struct BackgroundWork {
            std::string label;
            float fraction = 0;
        };

        struct BackgroundLoadResult {
            uint64_t generation = 0;
            std::string image_path;
            std::vector<std::unique_ptr<olc::Sprite>> levels;
            bool keep_image = false;
            std::optional<PreparedSongAudio> audio;
        };

        std::unique_ptr<DifficultyCache> difficulty_cache;

        ChartLibrary library;
        std::vector<size_t> visible;
        std::vector<std::pair<std::string, std::filesystem::path>> shuffle_history;
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
        Skin* logo_skin;
        EntityStarfield* sky;
        bool active = false;

        StatusBar status_bar;
        KeyRepeat up_repeat;
        KeyRepeat down_repeat;

        MipDecal background_image;
        MipDecal old_background_image;
        std::string background_image_path;

        uint64_t load_generation = 0;
        std::mutex background_load_mutex;
        std::condition_variable background_load_cv;
        bool background_load_stopping = false;
        std::optional<BackgroundLoadRequest> pending_background_load;
        std::optional<BackgroundLoadResult> completed_background_load;

        double timer = 0;

        bool crossfade = false;
        float crossfade_timer = 0;

        float pitch = 1.0;

        std::thread background_loader;

        std::mutex library_load_mutex;
        std::optional<ChartLibrary> completed_library_load;
        std::optional<LibraryUpdate> completed_library_update;
        std::unique_ptr<LibraryCache> library_cache;
        std::atomic<bool> library_load_finished = false;
        bool full_rebuild_pending = false;
        std::thread library_loader;
        bool library_loading = false;
        bool library_loaded = false;
        LibraryLoadProgress library_load_progress;
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
        std::vector<const Chart*> rate_charts;
        std::vector<float> rate_pitches;
        size_t rate_next = 0;
        size_t rate_busy = 0;
        bool rate_paused = false;
        std::vector<std::thread> rate_workers;

        float widest_grade = -1.0f;

        float background_work_time = 0;
        float background_work_opacity = 0;
        float background_work_fraction = 0;
        std::string background_work_label;

        StarSprite favorite_star;
        float favorite_star_ratio = -1.0f;
        LeaderboardPanel leaderboards;
        Hotspots row_spots;
        Hotspots ui_spots;
        ScrollBar list_scrollbar;
        ScrollBar preview_seek;
        double preview_seek_target = -1;
        bool show_leaderboards = false;
        bool recommended_started = false;

        void start_library_load();
        void reload_everything();
        void poll_library_load();
        void apply_library(ChartLibrary loaded);
        void apply_search(size_t keep_chart);
        void refresh_search();
        static ListSort current_sort();
        void set_sort(ListSort sort);
        void open_sort_menu();
        void sort_visible();
        void update_search();
        void draw_search(olc::PixelGameEngine* window);
        void select_chart(int32_t, bool sound = true);
        void create_preview_field();
        void set_pitch(float);
        std::optional<float> get_difficulty(size_t index) const;
        void difficulty_loop();
        void request_difficulties();
        void cancel_difficulties(bool wait);
        void rate_loop();
        void queue_all_rates();
        void pause_rates(bool paused);
        void collect_difficulties();
        std::optional<BackgroundWork> background_work();
        void update_background_work(float elapsed_time);

        void request_background_load();
        void apply_background_load(BackgroundLoadResult result);
        const MipDecal* background_for(const Chart& chart) const;
        void background_load_loop();
        void stop_preview_audio();
        void draw_list(olc::PixelGameEngine* window);
        std::string selected_hash() const;
        olc::vi2d side_panel_position(olc::vi2d screen) const;
        olc::vi2d side_panel_size(olc::vi2d screen) const;
        void download_recommended();
        void draw_recommended(olc::PixelGameEngine* window);
        bool update_controls(float elapsed_time);
        bool update_mouse(Input& input);
        bool update_preview_seek(Input& input);
        void draw_preview_seek(olc::PixelGameEngine* window);
        float list_position() const;
        size_t selected_library_index() const;
        void draw_list_scrollbar(olc::PixelGameEngine* window, int32_t charts_per_screen);
        bool run_status_action(StatusAction action, bool secondary);
        void play_selected();
        void open_chart_menu();
        void handle_chart_action(ChartAction action);
        void open_replay_menu();
        void watch_replay(Replay replay, std::string label);
        void toggle_favorite();
        size_t song_chart_count() const;
        size_t pack_chart_count() const;
        void open_chart_website();
        void delete_charts(ChartAction action);

        public:
        static constexpr size_t SHUFFLE_HISTORY_LIMIT = 100;
        static constexpr float PREVIEW_FADE_CURVE = 2.0f;
        static constexpr int32_t ROW_SPOT_CENTER = 1000;
        static constexpr int32_t SPOT_SEARCH_CLEAR = 1;
        static constexpr int32_t SPOT_DOWNLOAD = 2;
        static constexpr int32_t SPOT_BROWSE = 3;
        static constexpr int32_t SPOT_RECOMMENDED_FIRST = 100;
        static constexpr const char* DIFFICULTY_CACHE_FILE = "difficulty_cache.json";

        SceneList(Skin* skin, Skin* logo_skin, EntityStarfield* sky);
        std::optional<std::vector<std::filesystem::path>> chart_paths(bool favorites_only);
        bool library_available();
        float library_progress();
        ~SceneList();
        void on_create() override;
        void on_enter() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;

        const std::unordered_set<std::string>& get_local_hashes() const;
        void select_chart_by_hash(std::string hash, std::filesystem::path path, int playing_audio_id = -1);
    };
}
