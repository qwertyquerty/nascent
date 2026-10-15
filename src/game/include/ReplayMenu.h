#pragma once

#include "LocalReplays.h"
#include "Replay.h"
#include "ServerApi.h"
#include "gse/Overlay.h"
#include "olcPixelGameEngine.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Chart;
    class Skin;

    class ReplayMenu : public Overlay {
        public:
        struct Choice {
            std::optional<std::filesystem::path> path;
            int64_t play_id = 0;
            int64_t rank = 0;
            std::string player;
            double accuracy = 0;
            uint32_t max_combo = 0;
            float rate = 1.0f;
            std::string played_at;
        };

        using WatchCallback = std::function<void(Replay, std::string)>;

        static constexpr size_t MAX_ROWS = 8;
        static constexpr int32_t WIDTH = 760;
        static constexpr int32_t PADDING = 32;
        static constexpr int32_t ROW_HEIGHT = 40;
        static constexpr int32_t ROW_INSET = 14;
        static constexpr int32_t GAP = 14;
        static constexpr int32_t HEADER_HEIGHT = 44;
        static constexpr int32_t GRADE_OFFSET = 70;
        static constexpr int32_t ACCURACY_OFFSET = 120;
        static constexpr int32_t DETAIL_OFFSET = 260;

        ReplayMenu(Skin* skin, const Chart& chart, const std::string& gameplay_hash, float rate, WatchCallback on_watch);

        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        olc::Pixel backdrop() const override;

        private:
        Skin* skin;
        std::string chart_name;
        float rate = 1.0f;
        WatchCallback on_watch;
        size_t selected = 0;
        std::vector<Choice> local;
        std::vector<Choice> online;
        std::future<ApiResult<ChartLeaderboard>> board;
        std::string online_status = "Loading the leaderboard...";
        std::future<ApiResult<Replay>> download;
        std::optional<size_t> downloading;

        size_t count() const;
        const Choice& choice(size_t index) const;
        void watch(Replay replay, size_t index);
        static std::string label(const Choice& choice);
    };
}
