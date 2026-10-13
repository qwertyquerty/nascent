#pragma once

#include "Chart.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    inline constexpr double SEARCH_TOLERANCE = 0.5;

    struct SearchFilter {
        std::string field;
        std::string op;
        std::string value;
    };

    struct SearchQuery {
        std::vector<std::string> words;
        std::vector<SearchFilter> filters;

        bool filters_on(const std::string& field) const;
    };

    struct SearchSubject {
        const ChartInfo& info;
        const std::string& text;
        size_t note_count = 0;
        double length_seconds = 0;
        std::optional<double> difficulty;
        std::optional<double> best_accuracy;
    };

    std::string ascii_lowercase(std::string text);
    SearchQuery parse_search(const std::string& search);
    bool matches_search(const SearchQuery& query, const SearchSubject& subject);
}
