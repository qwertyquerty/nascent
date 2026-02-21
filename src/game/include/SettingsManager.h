#pragma once

#include "olcPixelGameEngine.h"
#include <string>

namespace nascent {
    struct Settings {
        uint16_t field_scroll_speed = 210; // vh / s
        uint8_t field_audio_sync_max_error = 20; // ms
        int8_t field_audio_visual_offset = -35; //ms
        int8_t field_audio_input_offset = 35; // ms

        float title_logo_start_delay = 1; // s
        uint8_t title_field_count = 9;
        uint16_t title_h_scroll_rate = 250; // px / s

        float play_start_delay = 2; // s

        std::string list_song_directory = "assets/songs";

        std::string discord_application_id = "1360933253070061718";
    };

    class SettingsManager {
        public:
        static Settings settings;
    };
}
