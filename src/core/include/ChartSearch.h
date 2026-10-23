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
        std::optional<double> number;
        std::optional<bool> flag;
        std::optional<int32_t> grade;
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
        std::optional<double> bpm;
        bool favorite = false;
        const std::string* artist_text = nullptr;
        const std::string* title_text = nullptr;
        const std::string* mapper_text = nullptr;
    };

    std::string ascii_lowercase(std::string text);
    std::string search_field_text(const ChartInfo& info, const std::string& field);
    SearchQuery parse_search(const std::string& search);
    bool matches_search(const SearchQuery& query, const SearchSubject& subject);
    bool query_narrows(const SearchQuery& before, const SearchQuery& after);
}
