#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace nascent {
    class Chart;
    class Skin;

    enum class ChartAction : uint8_t {
        WATCH_REPLAY,
        TOGGLE_FAVORITE,
        OPEN_WEBSITE,
        OPEN_FOLDER,
        DELETE_CHART,
        DELETE_PACK
    };

    class ChartMenu : public Overlay {
        protected:
        Skin* skin;
        std::string title;
        std::string subtitle;
        bool linked;
        std::string details;
        size_t pack_count;
        bool favorite;
        std::function<void(ChartAction)> on_action;
        size_t selected = 0;
        bool confirming = false;
        Hotspots spots;
        Rect panel;
        olc::vi2d last_mouse = {-1, -1};

        void activate();
        bool enabled(ChartAction action) const;
        void step_selection(bool down);

        std::string label(ChartAction action, bool confirming_action) const;

        public:
        static constexpr std::array<ChartAction, 6> ACTIONS = {ChartAction::WATCH_REPLAY, ChartAction::TOGGLE_FAVORITE, ChartAction::OPEN_WEBSITE, ChartAction::OPEN_FOLDER, ChartAction::DELETE_CHART, ChartAction::DELETE_PACK};

        static bool is_destructive(ChartAction action);

        ChartMenu(Skin* skin, const Chart& chart, size_t pack_count, bool favorite, std::function<void(ChartAction)> on_action);

        void on_open() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        olc::Pixel backdrop() const override;
    };
}
