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

    class LeaderboardPanel {
        template <typename T>
        struct Request {
            std::string key;
            uint64_t generation = 0;
            std::future<T> result;
        };

        uint64_t generation = 0;
        float delay = 0;
        std::unordered_map<std::string, std::vector<LocalScore>> local_scores;
        std::unordered_map<std::string, ApiResult<ChartLeaderboard>> online_scores;
        std::optional<Request<std::vector<LocalScore>>> local_request;
        std::optional<Request<ApiResult<ChartLeaderboard>>> online_request;

        public:
        static constexpr float SELECTION_DELAY = 0.25f;
        static constexpr int32_t ONLINE_LIMIT = 50;
        static constexpr int32_t LOCAL_ROWS = 8;
        static constexpr int32_t PADDING = 24;
        static constexpr int32_t ROW_HEIGHT = 30;
        static constexpr int32_t HEADER_HEIGHT = 52;
        static constexpr int32_t SECTION_GAP = 30;
        static constexpr int32_t GRADE_OFFSET = 56;
        static constexpr int32_t ACCURACY_OFFSET = 90;
        static constexpr int32_t NAME_OFFSET = 190;
        static inline const olc::Pixel BACKGROUND = olc::Pixel(0, 0, 0, 150);
        static inline const olc::Pixel HIGHLIGHT = olc::Pixel(255, 255, 255, 24);

        static std::string key(const std::string& gameplay_hash, float pitch);

        void clear();
        void wait_for_selection();
        void show_now();
        void update(float elapsed_time, bool shown, const std::string& gameplay_hash, float pitch);
        void draw(olc::PixelGameEngine* window, Skin* skin, const std::string& gameplay_hash, float pitch, olc::vi2d position, olc::vi2d size);
    };
}
