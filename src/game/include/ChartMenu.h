#pragma once

#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace nascent {
    class Chart;
    class Skin;

    enum class ChartAction : uint8_t {
        OPEN_WEBSITE,
        DELETE_CHART,
        DELETE_PACK
    };

    class ChartMenu {
        size_t selected = 0;
        bool confirming = false;

        public:
        struct Outcome {
            bool close = false;
            bool moved = false;
            std::optional<ChartAction> action;
        };

        static constexpr std::array<ChartAction, 3> ACTIONS = {ChartAction::OPEN_WEBSITE, ChartAction::DELETE_CHART, ChartAction::DELETE_PACK};
        static constexpr int32_t MIN_WIDTH = 720;
        static constexpr int32_t SCREEN_MARGIN = 120;
        static constexpr int32_t PADDING = 32;
        static constexpr int32_t ROW_HEIGHT = 44;
        static constexpr int32_t ROW_INSET = 14;
        static constexpr int32_t GAP = 14;

        Outcome update(olc::PixelGameEngine* window);
        void draw(olc::PixelGameEngine* window, Skin* skin, const Chart& chart, size_t pack_count) const;
    };
}
