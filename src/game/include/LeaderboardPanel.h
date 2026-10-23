#pragma once

#include "LocalReplays.h"
#include "ServerApi.h"
#include "gse/ui/Layout.h"
#include "gse/ui/ScrollBar.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nascent {
    class Skin;

    template <typename T>
    struct LeaderboardRequest {
        std::string key;
        uint64_t generation = 0;
        std::future<T> result;
    };

    struct LeaderboardPick {
        std::optional<std::filesystem::path> path;
        int64_t play_id = 0;
        std::string label;
    };

    enum class LeaderboardMode : uint8_t {
        ACCURACY,
        PERFORMANCE
    };

    struct LeaderboardSection {
        Rect area;
        int32_t rows = 0;
        int32_t count = 0;
        int32_t scroll = 0;
        ScrollBar bar;

        int32_t max_scroll() const;
        void clamp();
    };

    class LeaderboardPanel {
        uint64_t generation = 0;
        Hotspots spots;
        std::vector<LeaderboardPick> picks;
        LeaderboardMode mode = LeaderboardMode::ACCURACY;
        LeaderboardSection local_section;
        LeaderboardSection online_section;
        std::string scrolled_key;
        Rect panel_area;
        float delay = 0;
        std::unordered_map<std::string, std::vector<LocalScore>> local_scores;
        std::unordered_map<std::string, ApiResult<ChartLeaderboard>> online_scores;
        std::optional<LeaderboardRequest<std::vector<LocalScore>>> local_request;
        std::optional<LeaderboardRequest<ApiResult<ChartLeaderboard>>> online_request;

        public:
        static constexpr float SELECTION_DELAY = 0.25f;
        static constexpr int32_t ENTRY_LIMIT = 100;
        static constexpr int32_t LOCAL_ROWS = 8;
        static constexpr int32_t WHEEL_ROWS = 3;
        static constexpr int32_t MODE_SPOT = -1;

        std::string key(const std::string& gameplay_hash, float pitch) const;
        void set_mode(LeaderboardMode mode);
        LeaderboardMode get_mode() const;
        bool scroll(olc::vf2d point, int32_t wheel);
        bool update_scrollbars(Input& input);
        bool take_mode_click(Input& input);

        void clear();
        void wait_for_selection();
        void show_now();
        void update(float elapsed_time, bool shown, const std::string& gameplay_hash, float pitch);
        void draw(olc::PixelGameEngine* window, Skin* skin, const std::string& gameplay_hash, float pitch, olc::vi2d position, olc::vi2d size);
        std::optional<LeaderboardPick> take_click(Input& input);
    };
}
