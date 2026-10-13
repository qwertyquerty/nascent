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
            };

            for (const auto& [alias, name] : aliases) {
                if (field == alias) {
                    return name;
                }
            }

            return field;
        }

        bool is_search_field(const std::string& field) {
            static const char* fields[] = {"keys", "difficulty", "length", "notes", "played", "grade", "artist", "title", "mapper"};
            return std::any_of(std::begin(fields), std::end(fields), [&](const char* name) { return field == name; });
        }

        bool matches_filter(const SearchFilter& filter, const SearchSubject& subject) {
            if (filter.value.empty()) {
                return true;
            }

            if (filter.field == "artist" || filter.field == "title" || filter.field == "mapper") {
                const ChartInfo& info = subject.info;
                std::string text = ascii_lowercase(filter.field == "artist" ? info.artist + " " + info.artist_unicode : filter.field == "title" ? info.title + " " + info.title_unicode
                                                                                                                                                : info.mapper);
                bool contains = text.find(filter.value) != std::string::npos;
                return filter.op == "!=" ? !contains : filter.op == "=" ? contains
                                                                        : true;
            }

            if (filter.field == "played") {
                std::optional<bool> wanted = parse_bool(filter.value);

                if (!wanted || (filter.op != "=" && filter.op != "!=")) {
                    return true;
                }

                return (subject.best_accuracy.has_value() == *wanted) == (filter.op == "=");
            }

            if (filter.field == "grade") {
                std::optional<int32_t> wanted = parse_grade(filter.value);

                if (!wanted) {
                    return true;
                }

                return subject.best_accuracy && compare(-(double)grade_for_accuracy(*subject.best_accuracy), filter.op, *wanted, 0.0);
            }

            std::optional<double> wanted = parse_number(filter.value);

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
                    query.filters.push_back({field, op, lowered.substr(op_end)});
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

    bool matches_search(const SearchQuery& query, const SearchSubject& subject) {
        bool words = std::all_of(query.words.begin(), query.words.end(), [&](const std::string& word) {
            return subject.text.find(word) != std::string::npos;
        });

        return words && std::all_of(query.filters.begin(), query.filters.end(), [&](const SearchFilter& filter) {
                   return matches_filter(filter, subject);
               });
    }
}
