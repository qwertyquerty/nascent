#pragma once

#include "LocalReplays.h"
#include "Replay.h"
#include "ServerApi.h"
#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
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

        ReplayMenu(Skin* skin, const Chart& chart, const std::string& gameplay_hash, float rate, WatchCallback on_watch);

        void on_open() override;
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
        Hotspots spots;
        Rect panel;
        olc::vi2d last_mouse = {-1, -1};

        void choose();

        size_t count() const;
        const Choice& choice(size_t index) const;
        void watch(Replay replay, size_t index);
        static std::string label(const Choice& choice);
    };
}
