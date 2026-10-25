#pragma once

#include "Hit.h"

#include <cstdint>
#include <format>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace nascent {
    inline constexpr double CHART_MAX_BPM = 9999999.0;
    inline constexpr int32_t CHART_MIN_SAME_LANE_GAP_MS = 5;
    inline constexpr std::string_view CHART_INVALID_BPM = "BPMs must be above 0 and at most 9,999,999";

    inline bool valid_chart_bpm(double bpm) {
        return bpm > 0 && bpm <= CHART_MAX_BPM;
    }

    inline void validate_chart_hits(const std::vector<Hit>& hits, bool allow_empty = false) {
        if (hits.empty() && !allow_empty) {
            throw std::runtime_error("chart has no notes");
        }

        std::map<uint8_t, const Hit*> previous;

        for (const Hit& hit : hits) {
            if (hit.time < 0) {
                throw std::runtime_error("chart has notes before the song starts");
            }

            auto found = previous.find(hit.key);

            if (found != previous.end()) {
                const Hit& last = *found->second;

                if (last.hit_type == HitType::HOLD && hit.time <= last.end_time) {
                    throw std::runtime_error("a note starts during a hold in the same column");
                }

                if (hit.time - last.time <= CHART_MIN_SAME_LANE_GAP_MS) {
                    throw std::runtime_error(std::format("two notes in the same column are {} ms or less apart", CHART_MIN_SAME_LANE_GAP_MS));
                }
            }

            previous[hit.key] = &hit;
        }
    }
}
