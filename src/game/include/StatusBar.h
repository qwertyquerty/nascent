#pragma once

#include "ServerApi.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <future>
#include <optional>
#include <string>

namespace nascent {
    class Skin;

    class StatusBar {
        protected:
        enum class Connection {
            CHECKING,
            ONLINE,
            DEGRADED,
            OFFLINE
        };

        Connection connection = Connection::CHECKING;
        double latency_ms = 0;
        float heartbeat_timer = 0;
        std::future<ApiResult<ServerHealth>> heartbeat;

        std::string username;
        std::string requested_username;
        std::optional<Profile> profile;
        float profile_timer = 0;
        std::future<ApiResult<Profile>> profile_request;

        uint8_t key_count = 0;
        float rate = 1.0f;

        void poll_heartbeat();
        void poll_profile();

        public:
        static constexpr float HEARTBEAT_INTERVAL = 15.0f;
        static constexpr float PROFILE_INTERVAL = 60.0f;
        static constexpr int32_t HEIGHT = 40;
        static constexpr int32_t PADDING = 20;
        static constexpr int32_t SEGMENT_GAP = 36;
        static constexpr int32_t LABEL_GAP = 8;
        static constexpr int32_t DOT_RADIUS = 4;
        static inline const olc::Pixel BACKGROUND = olc::Pixel(0, 0, 0, 213);
        static inline const olc::Pixel EDGE = olc::Pixel(255, 255, 255, 30);

        void refresh();
        void set_key_count(uint8_t key_count);
        void set_rate(float rate);
        void update(float elapsed_time);
        void draw(olc::PixelGameEngine* window, Skin* skin) const;
    };
}
