#include "Timestamps.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <optional>
#include <regex>

namespace nascent {
    namespace {
        std::optional<int32_t> number(std::string_view text) {
            int32_t value = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            return error == std::errc() && end == text.data() + text.size() ? std::optional<int32_t>(value) : std::nullopt;
        }

        std::vector<std::pair<int32_t, uint8_t>> note_list(std::string_view text) {
            std::vector<std::pair<int32_t, uint8_t>> notes;

            while (!text.empty()) {
                size_t comma = text.find(',');
                std::string_view item = text.substr(0, comma);
                size_t bar = item.find('|');

                if (bar != std::string_view::npos) {
                    std::optional<int32_t> time = number(item.substr(0, bar));
                    std::optional<int32_t> lane = number(item.substr(bar + 1));

                    if (time && lane && *lane >= 0 && *lane < 256) {
                        notes.push_back({*time, (uint8_t)*lane});
                    }
                }

                text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
            }

            return notes;
        }
    }

    std::string format_clock(int32_t time) {
        int32_t clamped = std::max(0, time);
        return std::format("{:02}:{:02}:{:03}", clamped / 60000, clamped / 1000 % 60, clamped % 1000);
    }

    std::string format_timestamp(int32_t time, const std::vector<Hit>& notes) {
        if (notes.empty()) {
            return format_clock(time) + " - ";
        }

        std::vector<Hit> sorted = notes;
        std::sort(sorted.begin(), sorted.end(), [](const Hit& a, const Hit& b) { return std::make_pair(a.time, a.key) < std::make_pair(b.time, b.key); });
        std::string list;

        for (const Hit& hit : sorted) {
            list += std::format("{}{}|{}", list.empty() ? "" : ",", hit.time, hit.key);
        }

        return std::format("{} ({}) - ", format_clock(sorted.front().time), list);
    }

    std::vector<Timestamp> find_timestamps(std::string_view text) {
        static const std::regex PATTERN(R"((\d+):(\d{2}):(\d{3})(?:\s*\(([^()]*)\))?)");
        std::vector<Timestamp> found;
        std::string owned(text);

        for (auto match = std::sregex_iterator(owned.begin(), owned.end(), PATTERN); match != std::sregex_iterator(); ++match) {
            std::optional<int32_t> minutes = number((*match)[1].str());
            std::optional<int32_t> seconds = number((*match)[2].str());
            std::optional<int32_t> millis = number((*match)[3].str());

            if (!minutes || !seconds || !millis || *seconds >= 60) {
                continue;
            }

            Timestamp timestamp;
            timestamp.time = *minutes * 60000 + *seconds * 1000 + *millis;
            timestamp.notes = (*match)[4].matched ? note_list((*match)[4].str()) : std::vector<std::pair<int32_t, uint8_t>>();
            timestamp.start = (size_t)match->position(0);
            timestamp.length = (size_t)match->length(0);
            found.push_back(std::move(timestamp));
        }

        return found;
    }
}
