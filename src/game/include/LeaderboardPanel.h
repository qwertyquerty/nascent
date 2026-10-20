#pragma once

#include "LocalReplays.h"
#include "ServerApi.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
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

    class LeaderboardPanel {
        uint64_t generation = 0;
        float delay = 0;
        std::unordered_map<std::string, std::vector<LocalScore>> local_scores;
        std::unordered_map<std::string, ApiResult<ChartLeaderboard>> online_scores;
        std::optional<LeaderboardRequest<std::vector<LocalScore>>> local_request;
        std::optional<LeaderboardRequest<ApiResult<ChartLeaderboard>>> online_request;

        public:
        static constexpr float SELECTION_DELAY = 0.25f;
        static constexpr int32_t ONLINE_LIMIT = 50;
        static constexpr int32_t LOCAL_ROWS = 8;

        static std::string key(const std::string& gameplay_hash, float pitch);

        void clear();
        void wait_for_selection();
        void show_now();
        void update(float elapsed_time, bool shown, const std::string& gameplay_hash, float pitch);
        void draw(olc::PixelGameEngine* window, Skin* skin, const std::string& gameplay_hash, float pitch, olc::vi2d position, olc::vi2d size);
    };
}
