#include "ChartSearch.h"

#include "Results.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <locale>
#include <sstream>
#include <utility>

namespace nascent {
    namespace {
        std::optional<double> parse_number(const std::string& text) {
            if (text.empty()) {
                return std::nullopt;
            }

            size_t colon = text.find(':');

            if (colon != std::string::npos) {
                std::optional<double> minutes = parse_number(text.substr(0, colon));
                std::optional<double> seconds = parse_number(text.substr(colon + 1));
                return minutes && seconds ? std::optional<double>(*minutes * 60.0 + *seconds) : std::nullopt;
            }

            std::istringstream stream(text);
            stream.imbue(std::locale::classic());
            double value = 0;
            stream >> value;
            return stream.fail() || !stream.eof() ? std::nullopt : std::optional<double>(value);
        }

        std::optional<bool> parse_bool(const std::string& text) {
            if (text == "true" || text == "yes" || text == "1") {
                return true;
            }

            if (text == "false" || text == "no" || text == "0") {
                return false;
            }

            return std::nullopt;
        }

        std::optional<int32_t> parse_grade(const std::string& text) {
            for (Grade grade : {Grade::X, Grade::S, Grade::A, Grade::B, Grade::C, Grade::D, Grade::E}) {
                if (ascii_lowercase(std::string(grade_name(grade))) == text) {
                    return -(int32_t)grade;
                }
            }

            return std::nullopt;
        }

        bool compare(double actual, const std::string& op, double expected, double tolerance) {
            if (op == "=") {
                return std::abs(actual - expected) <= tolerance;
            }

            if (op == "!=") {
                return std::abs(actual - expected) > tolerance;
            }

            if (op == "<") {
                return actual < expected;
            }

            if (op == ">") {
                return actual > expected;
            }

            if (op == "<=") {
                return actual <= expected;
            }

            return actual >= expected;
        }

        std::string canonical_field(const std::string& field) {
            static const std::pair<const char*, const char*> aliases[] = {
                {"diff", "difficulty"},
                {"len", "length"},
                {"key", "keys"},
                {"note", "notes"},
                {"fav", "favorite"},
                {"favourite", "favorite"},
            };

            for (const auto& [alias, name] : aliases) {
                if (field == alias) {
                    return name;
                }
            }

            return field;
        }

        bool is_search_field(const std::string& field) {
            static const char* fields[] = {"keys", "difficulty", "length", "notes", "played", "grade", "artist", "title", "mapper", "bpm", "favorite"};
            return std::any_of(std::begin(fields), std::end(fields), [&](const char* name) { return field == name; });
        }

        bool matches_filter(const SearchFilter& filter, const SearchSubject& subject) {
            if (filter.value.empty()) {
                return true;
            }

            if (filter.field == "artist" || filter.field == "title" || filter.field == "mapper") {
                const std::string* precomputed = filter.field == "artist" ? subject.artist_text : filter.field == "title" ? subject.title_text
                                                                                                                          : subject.mapper_text;
                std::string computed = precomputed == nullptr ? search_field_text(subject.info, filter.field) : std::string();
                const std::string& text = precomputed != nullptr ? *precomputed : computed;
                bool contains = text.find(filter.value) != std::string::npos;
                return filter.op == "!=" ? !contains : filter.op == "=" ? contains
                                                                        : true;
            }

            if (filter.field == "played" || filter.field == "favorite") {
                const std::optional<bool>& wanted = filter.flag;

                if (!wanted || (filter.op != "=" && filter.op != "!=")) {
                    return true;
                }

                bool actual = filter.field == "played" ? subject.best_accuracy.has_value() : subject.favorite;
                return (actual == *wanted) == (filter.op == "=");
            }

            if (filter.field == "grade") {
                const std::optional<int32_t>& wanted = filter.grade;

                if (!wanted) {
                    return true;
                }

                return subject.best_accuracy && compare(-(double)grade_for_accuracy(*subject.best_accuracy), filter.op, *wanted, 0.0);
            }

            const std::optional<double>& wanted = filter.number;

            if (!wanted) {
                return true;
            }

            if (filter.field == "keys") {
                return compare(subject.info.key_count, filter.op, *wanted, 0.0);
            }

            if (filter.field == "notes") {
                return compare((double)subject.note_count, filter.op, *wanted, 0.0);
            }

            if (filter.field == "length") {
                return compare(subject.length_seconds, filter.op, *wanted, SEARCH_TOLERANCE);
            }

            if (filter.field == "bpm") {
                return subject.bpm && compare(*subject.bpm, filter.op, *wanted, SEARCH_TOLERANCE);
            }

            return subject.difficulty && compare(*subject.difficulty, filter.op, *wanted, SEARCH_TOLERANCE);
        }
    }

    bool SearchQuery::filters_on(const std::string& field) const {
        return std::any_of(filters.begin(), filters.end(), [&](const SearchFilter& filter) { return filter.field == field; });
    }

    std::string ascii_lowercase(std::string text) {
        for (char& c : text) {
            c = (char)std::tolower((unsigned char)c);
        }

        return text;
    }

    std::string search_field_text(const ChartInfo& info, const std::string& field) {
        if (field == "artist") {
            return ascii_lowercase(info.artist + " " + info.artist_unicode);
        }

        if (field == "title") {
            return ascii_lowercase(info.title + " " + info.title_unicode);
        }

        return ascii_lowercase(info.mapper);
    }

    SearchQuery parse_search(const std::string& search) {
        SearchQuery query;
        std::string token;

        auto finish = [&]() {
            if (token.empty()) {
                return;
            }

            std::string lowered = ascii_lowercase(token);
            size_t op_start = lowered.find_first_of("<>=!");

            if (op_start != std::string::npos && op_start > 0) {
                size_t op_end = op_start + 1;

                if (op_end < lowered.size() && lowered[op_end] == '=' && lowered[op_start] != '=') {
                    op_end++;
                }

                std::string field = canonical_field(lowered.substr(0, op_start));
                std::string op = lowered.substr(op_start, op_end - op_start);

                if (is_search_field(field) && op != "!") {
                    SearchFilter filter{field, op, lowered.substr(op_end)};

                    if (field == "played" || field == "favorite") {
                        filter.flag = parse_bool(filter.value);
                    }
                    else if (field == "grade") {
                        filter.grade = parse_grade(filter.value);
                    }
                    else if (field != "artist" && field != "title" && field != "mapper") {
                        filter.number = parse_number(filter.value);
                    }

                    query.filters.push_back(std::move(filter));
                    token.clear();
                    return;
                }
            }

            query.words.push_back(std::move(lowered));
            token.clear();
        };

        for (char c : search) {
            if (c == ' ') {
                finish();
            }
            else {
                token.push_back(c);
            }
        }

        finish();
        return query;
    }

    bool query_narrows(const SearchQuery& before, const SearchQuery& after) {
        if (before.filters.size() != after.filters.size() || after.words.size() < before.words.size()) {
            return false;
        }

        for (size_t index = 0; index < before.filters.size(); index++) {
            const SearchFilter& a = before.filters[index];
            const SearchFilter& b = after.filters[index];

            if (a.field != b.field || a.op != b.op || a.value != b.value) {
                return false;
            }
        }

        for (size_t index = 0; index < before.words.size(); index++) {
            if (after.words[index].find(before.words[index]) == std::string::npos) {
                return false;
            }
        }

        return true;
    }

    bool matches_search(const SearchQuery& query, const SearchSubject& subject) {
        bool words = std::all_of(query.words.begin(), query.words.end(), [&](const std::string& word) {
            return subject.text.find(word) != std::string::npos;
        });

        return words && std::all_of(query.filters.begin(), query.filters.end(), [&](const SearchFilter& filter) {
                   return matches_filter(filter, subject);
               });
    }
}
