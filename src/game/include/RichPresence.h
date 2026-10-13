#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace nascent {
    class Chart;

    class RichPresence {
        public:
        static constexpr float PROFILE_INTERVAL = 60.0f;
        static constexpr float MIN_UPDATE_INTERVAL = 2.0f;
        static constexpr size_t MAX_TEXT_LENGTH = 128;
        static constexpr const char* IMAGE_KEY = "logo";

        static void set_enabled(bool enabled);
        static void update(float elapsed_time);
        static void shutdown();
        static void refresh_profile();

        static void in_menu(std::string activity, uint8_t key_count = 0);
        static void picking_song(const Chart* chart);
        static void playing(const Chart& chart);
        static void viewing_results(const Chart& chart, double accuracy, std::string_view grade);
    };
}
