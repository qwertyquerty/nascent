#pragma once

#include <cstdint>
#include <string>

namespace nascent {
    class Favorites {
        public:
        static constexpr const char* FILE_NAME = "favorites.json";
        static constexpr uint32_t FILE_VERSION = 1;

        static void load();
        static bool contains(const std::string& chart_hash);
        static bool toggle(const std::string& chart_hash);
    };
}
