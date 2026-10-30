#pragma once

#include "ChartDocument.h"
#include "Hit.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace nascent {
    struct LibraryNote {
        double beat = 0;
        uint8_t key = 0;
        double length = 0;
    };

    struct LibraryPattern {
        std::string name;
        uint8_t key_count = ChartDocument::DEFAULT_KEY_COUNT;
        std::vector<LibraryNote> notes;
    };

    LibraryPattern capture_pattern(const ChartDocument& document, const std::vector<Hit>& hits, std::string name);
    std::vector<Hit> library_pattern_hits(const ChartDocument& document, const LibraryPattern& pattern, double at, int32_t lane_offset);
    double pattern_length_beats(const LibraryPattern& pattern);
    std::pair<uint8_t, uint8_t> pattern_lanes(const LibraryPattern& pattern);
    std::vector<LibraryPattern> load_pattern_library(const std::filesystem::path& path, std::string& error);
    bool save_pattern_library(const std::filesystem::path& path, const std::vector<LibraryPattern>& patterns, std::string& error);
}
