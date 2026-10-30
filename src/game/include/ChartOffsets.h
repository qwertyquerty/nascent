#pragma once

#include <cstdint>
#include <string>

namespace nascent {
    class ChartOffsets {
        public:
        static constexpr const char* FILE_NAME = "chart_offsets.json";
        static constexpr uint32_t FILE_VERSION = 1;
        static constexpr int32_t LIMIT_MS = 300;

        static void load();
        static int32_t get(const std::string& chart_hash);
        static int32_t set(const std::string& chart_hash, int32_t offset);
    };
}
