#pragma once

#include "gse/Overlay.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace nascent {
    class Skin;

    enum class ListSort : uint8_t {
        TITLE,
        ARTIST,
        DIFFICULTY,
        BPM,
        LENGTH,
        DATE_ADDED,
        KEY_COUNT,
        LONG_NOTES,
        RECENTLY_PLAYED,
        BEST_ACCURACY,
        COUNT
    };

    inline constexpr std::array<const char*, (size_t)ListSort::COUNT> LIST_SORT_NAMES = {"Title", "Artist", "Difficulty", "BPM", "Length", "Date added", "Keys", "Long notes", "Recently played", "Best accuracy"};

    class SortMenu : public Overlay {
        protected:
        Skin* skin;
        ListSort current;
        std::function<void(ListSort)> on_choose;
        size_t selected = 0;
        Hotspots spots;
        Rect panel;
        olc::vi2d last_mouse = {-1, -1};

        void choose();

        public:
        static constexpr const char* TITLE = "Sort songs by";

        SortMenu(Skin* skin, ListSort current, std::function<void(ListSort)> on_choose);

        void on_open() override;
        void update(float elapsed_time) override;
        void draw(olc::PixelGameEngine* window) override;
        olc::Pixel backdrop() const override;
    };
}
