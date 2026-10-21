#include "DifficultyEvents.h"
#include "DifficultyBasis.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>
#include <utility>

namespace nascent {
    namespace {
        constexpr std::array<int, 10> SNAP_DIVISORS = {1, 2, 4, 3, 8, 6, 12, 16, 24, 32};
        constexpr std::array<double, 10> SNAP_VALUES = {0.0, 0.0, 0.1, 0.5, 0.3, 0.6, 0.8, 0.7, 0.9, 0.9};
        constexpr double OFF_GRID_SNAP = 1.0;
        constexpr double SNAP_TOLERANCE_MS = 3.0;
        constexpr size_t MAX_HANDS = 3;

        struct Press {
            double time;
            uint8_t column;
            std::optional<double> end;
            size_t id = 0;
            double weight = 0;
        };

        struct BuiltEvent {
            size_t id;
            double weight;
            DifficultyEvent event;
        };

        double chord_weight(double gap) {
            return std::clamp((CHORD_SEPARATE_MS - gap) / (CHORD_SEPARATE_MS - CHORD_MERGED_MS), 0.0, 1.0);
        }

        double blend_gap(double separate, double merged, double weight) {
            if (separate == merged) {
                return separate;
            }

            auto rate = [](double gap) {
                return gap >= NO_PREVIOUS_GAP - 0.5 ? 0.0 : 1.0 / std::max(gap, BASIS_MIN_GAP_MS);
            };

            double blended = (1.0 - weight) * rate(separate) + weight * rate(merged);
            return blended > 0 ? 1.0 / blended : NO_PREVIOUS_GAP;
        }

        DifficultyEvent blend_events(const DifficultyEvent& separate, const DifficultyEvent& merged, double weight) {
            DifficultyEvent result = separate;

            for (size_t channel = 0; channel < EVENT_CHANNEL_COUNT; channel++) {
                result[channel] = (1.0 - weight) * separate[channel] + weight * merged[channel];
            }

            for (EventChannel channel : {EventChannel::COLUMN_GAP, EventChannel::PREVIOUS_COLUMN_GAP, EventChannel::HAND_GAP, EventChannel::PREVIOUS_HAND_GAP, EventChannel::ONSET_GAP, EventChannel::PREVIOUS_ONSET_GAP, EventChannel::OTHER_HAND_GAP, EventChannel::NEAREST_HAND_EVENT}) {
                result[(size_t)channel] = blend_gap(separate[(size_t)channel], merged[(size_t)channel], weight);
            }

            for (EventChannel channel : {EventChannel::COLUMN, EventChannel::HAND, EventChannel::RELEASE, EventChannel::HOLD, EventChannel::KEY_COUNT}) {
                result[(size_t)channel] = separate[(size_t)channel];
            }

            return result;
        }

        class HoldSweep {
            const std::vector<Press>& holds;
            uint8_t key_count;
            size_t next = 0;
            std::vector<size_t> active;

            public:
            HoldSweep(const std::vector<Press>& holds, uint8_t key_count) : holds(holds), key_count(key_count) {}

            std::pair<double, double> held_at(double time, uint8_t column) {
                while (next < holds.size() && holds[next].time < time) {
                    active.push_back(next++);
                }

                std::erase_if(active, [&](size_t index) {
                    return *holds[index].end <= time;
                });

                uint8_t hand = difficulty_hand(column, key_count);
                double same_hand = 0;
                double total = 0;

                for (size_t index : active) {
                    if (holds[index].column == column) {
                        continue;
                    }

                    total += 1;
                    same_hand += difficulty_hand(holds[index].column, key_count) == hand ? 1 : 0;
                }

                return {same_hand, total};
            }
        };

        double gap_since(std::optional<double> last, double time) {
            return last ? time - *last : NO_PREVIOUS_GAP;
        }

        double previous_before(const std::vector<double>& times, double time) {
            auto it = std::lower_bound(times.begin(), times.end(), time);
            return it == times.begin() ? NO_PREVIOUS_GAP : time - *std::prev(it);
        }

        double nearest_other(const std::vector<double>& times, double time) {
            auto it = std::lower_bound(times.begin(), times.end(), time);
            double nearest = NO_PREVIOUS_GAP;

            if (it != times.begin()) {
                nearest = std::min(nearest, time - *std::prev(it));
            }

            auto after = std::upper_bound(times.begin(), times.end(), time);

            if (after != times.end()) {
                nearest = std::min(nearest, *after - time);
            }

            return nearest;
        }
    }

    uint8_t difficulty_hand(uint8_t column, uint8_t key_count) {
        uint8_t half = key_count / 2;

        if (key_count % 2 == 1 && column == half) {
            return 2;
        }

        return column < half ? 0 : 1;
    }

    double snap_complexity(double time, const std::vector<ChartTimingPoint>& timing_points) {
        if (timing_points.empty()) {
            return 0.0;
        }

        auto after = std::partition_point(timing_points.begin(), timing_points.end(), [time](const ChartTimingPoint& point) {
            return point.time <= time + 1;
        });
        const ChartTimingPoint& point = after == timing_points.begin() ? timing_points.front() : *std::prev(after);

        if (point.beat_length <= 0) {
            return 0.0;
        }

        return grid_snap(time, (double)point.time, (double)point.beat_length);
    }

    double grid_snap(double time, double origin, double beat_length) {
        double beats = (time - origin) / beat_length;
        double fraction = beats - std::floor(beats);
        double tolerance = SNAP_TOLERANCE_MS / beat_length;

        for (size_t index = 0; index < SNAP_DIVISORS.size(); index++) {
            double position = fraction * SNAP_DIVISORS[index];

            if (std::abs(position - std::round(position)) / SNAP_DIVISORS[index] < tolerance) {
                return SNAP_VALUES[index];
            }
        }

        return OFF_GRID_SNAP;
    }

    double roll_evenness(const std::vector<std::pair<uint8_t, double>>& window, uint8_t key_count) {
        if (window.size() < ROLL_MIN_NOTES) {
            return 0.0;
        }

        std::array<double, 256> counts = {};
        double sum = 0;
        double squares = 0;

        for (size_t index = 0; index < window.size(); index++) {
            auto [column, gap] = window[index];

            if (column >= key_count || gap >= NO_PREVIOUS_GAP - 0.5 || gap <= 0 || (index > 0 && window[index - 1].first == column)) {
                return 0.0;
            }

            counts[column] += 1;
            sum += gap;
            squares += gap * gap;
        }

        double count = (double)window.size();
        double mean = sum / count;
        double deviation = std::sqrt(std::max(0.0, squares / count - mean * mean));
        double fewest = count;
        double most = 0;
        size_t used = 0;

        for (size_t column = 0; column < key_count; column++) {
            double notes = counts[column];

            if (notes > 0) {
                fewest = std::min(fewest, notes);
                most = std::max(most, notes);
                used++;
            }
        }

        if (used < 2) {
            return 0.0;
        }

        return std::clamp(1.0 - deviation / mean / ROLL_CV_LIMIT, 0.0, 1.0) * fewest / most;
    }

    double pattern_variety(const std::vector<uint32_t>& masks) {
        if (masks.size() < VARIETY_MIN_ONSETS) {
            return 0.0;
        }

        std::array<uint64_t, VARIETY_WINDOW> buffer;
        std::vector<uint64_t> overflow;
        size_t total = masks.size() - 1;
        uint64_t* pairs = buffer.data();

        if (total > buffer.size()) {
            overflow.resize(total);
            pairs = overflow.data();
        }

        for (size_t index = 1; index < masks.size(); index++) {
            pairs[index - 1] = ((uint64_t)masks[index - 1] << 32) | masks[index];
        }

        std::sort(pairs, pairs + total);
        size_t distinct = (size_t)std::distance(pairs, std::unique(pairs, pairs + total));
        return (double)(distinct - 1) / (double)(total - 1);
    }

    static std::vector<BuiltEvent> build_events(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, double tolerance) {
        std::vector<BuiltEvent> events;

        if (hits.empty() || key_count == 0) {
            return events;
        }

        std::vector<Press> presses;
        presses.reserve(hits.size());

        for (size_t index = 0; index < hits.size(); index++) {
            const Hit& hit = hits[index];

            if (hit.key >= key_count) {
                continue;
            }

            bool hold = hit.hit_type == HitType::HOLD && hit.end_time > hit.time;
            presses.push_back({(double)hit.time, hit.key, hold ? std::optional<double>(hit.end_time) : std::nullopt, index, 0.0});
        }

        std::stable_sort(presses.begin(), presses.end(), [](const Press& a, const Press& b) {
            return a.time < b.time || (a.time == b.time && a.column < b.column);
        });

        size_t group_start = 0;
        double group_time = 0;
        uint32_t group_columns = 0;

        for (size_t index = 0; tolerance > 0 && index < presses.size(); index++) {
            Press& press = presses[index];
            uint32_t bit = 1u << press.column;

            if (group_columns != 0 && press.time - group_time <= tolerance && (group_columns & bit) == 0) {
                press.weight = chord_weight(press.time - group_time);
                presses[group_start].weight = std::max(presses[group_start].weight, press.weight);
                press.time = group_time;
                group_columns |= bit;
            }
            else {
                group_start = index;
                group_time = press.time;
                group_columns = bit;
            }
        }

        std::stable_sort(presses.begin(), presses.end(), [](const Press& a, const Press& b) {
            return a.time < b.time || (a.time == b.time && a.column < b.column);
        });

        std::vector<double> jack_runs(presses.size(), 1.0);
        std::vector<std::vector<size_t>> column_presses(key_count);
        std::vector<double> onsets;

        for (size_t index = 0; index < presses.size(); index++) {
            column_presses[presses[index].column].push_back(index);

            if (onsets.empty() || presses[index].time != onsets.back()) {
                onsets.push_back(presses[index].time);
            }
        }

        for (const std::vector<size_t>& order : column_presses) {
            std::optional<size_t> first;

            auto close = [&](size_t end) {
                if (first) {
                    for (size_t member = *first; member < end; member++) {
                        jack_runs[order[member]] = (double)(end - *first + 1);
                    }
                }

                first.reset();
            };

            for (size_t position = 1; position < order.size(); position++) {
                double time = presses[order[position]].time;
                double previous_time = presses[order[position - 1]].time;
                auto onset = std::lower_bound(onsets.begin(), onsets.end(), time);
                bool jack = onset != onsets.begin() && *std::prev(onset) == previous_time;

                if (!jack) {
                    close(position);
                    continue;
                }

                if (first && position > *first) {
                    double gap = time - previous_time;
                    double previous = previous_time - presses[order[position - 2]].time;

                    if (gap > previous * JACK_RUN_RATIO || gap * JACK_RUN_RATIO < previous) {
                        close(position);
                    }
                }

                if (!first) {
                    first = position;
                }
            }

            close(order.size());
        }

        std::array<std::vector<double>, MAX_HANDS> hand_times;

        for (const Press& press : presses) {
            uint8_t hand = difficulty_hand(press.column, key_count);
            hand_times[hand].push_back(press.time);

            if (press.end) {
                hand_times[hand].push_back(*press.end);
            }
        }

        for (std::vector<double>& times : hand_times) {
            std::sort(times.begin(), times.end());
            times.erase(std::unique(times.begin(), times.end()), times.end());
        }

        std::vector<std::optional<double>> column_last(key_count);
        std::vector<double> column_last_gap(key_count, NO_PREVIOUS_GAP);
        std::array<std::optional<double>, MAX_HANDS> hand_last;
        std::array<double, MAX_HANDS> hand_last_gap;
        hand_last_gap.fill(NO_PREVIOUS_GAP);
        std::optional<double> onset_last;
        double onset_last_gap = NO_PREVIOUS_GAP;
        uint32_t previous_columns = 0;
        uint32_t previous_hands = 0;
        std::array<std::vector<std::pair<uint8_t, double>>, MAX_HANDS> roll_windows;
        std::array<double, MAX_HANDS> hand_evenness = {};
        std::vector<uint32_t> onset_masks;

        std::vector<Press> holds;
        std::copy_if(presses.begin(), presses.end(), std::back_inserter(holds), [](const Press& press) {
            return press.end.has_value();
        });
        HoldSweep press_sweep(holds, key_count);

        for (size_t first = 0; first < presses.size();) {
            double time = presses[first].time;
            size_t last = first;
            uint32_t columns = 0;
            uint32_t hands = 0;
            std::array<double, MAX_HANDS> hand_counts = {};

            while (last < presses.size() && presses[last].time == time) {
                uint8_t hand = difficulty_hand(presses[last].column, key_count);
                columns |= 1u << presses[last].column;
                hands |= 1u << hand;
                hand_counts[hand] += 1;
                last++;
            }

            double chord_size = (double)(last - first);
            double overlap = std::popcount(columns & previous_columns) / chord_size;
            double onset_gap = gap_since(onset_last, time);
            onset_masks.push_back(columns);

            if (onset_masks.size() > VARIETY_WINDOW) {
                onset_masks.erase(onset_masks.begin());
            }

            double variety = pattern_variety(onset_masks);

            for (size_t hand = 0; hand < MAX_HANDS; hand++) {
                if (hand_counts[hand] > 1) {
                    roll_windows[hand].clear();
                    hand_evenness[hand] = 0;
                }
            }

            for (size_t index = first; index < last; index++) {
                const Press& press = presses[index];
                uint8_t hand = difficulty_hand(press.column, key_count);
                double other_hand_gap = NO_PREVIOUS_GAP;

                for (size_t other = 0; other < MAX_HANDS; other++) {
                    if (other != hand && hand_last[other]) {
                        other_hand_gap = std::min(other_hand_gap, time - *hand_last[other]);
                    }
                }

                double hand_gap = gap_since(hand_last[hand], time);
                double roll = 0;

                if (hand_counts[hand] == 1) {
                    double finger_gap = gap_since(column_last[press.column], time);

                    if (!roll_windows[hand].empty()) {
                        double previous_gap = roll_windows[hand].back().second;

                        if (finger_gap > previous_gap * ROLL_BREAK_RATIO || finger_gap * ROLL_BREAK_RATIO < previous_gap) {
                            roll_windows[hand].clear();
                        }
                    }

                    roll_windows[hand].push_back({press.column, finger_gap});

                    if (roll_windows[hand].size() > ROLL_WINDOW) {
                        roll_windows[hand].erase(roll_windows[hand].begin());
                    }

                    hand_evenness[hand] = roll_evenness(roll_windows[hand], key_count);
                    double partner = 0;

                    for (size_t other = 0; other < MAX_HANDS; other++) {
                        if (other != hand && hand_last[other] && time - *hand_last[other] < finger_gap) {
                            partner = std::max(partner, hand_evenness[other]);
                        }
                    }

                    roll = std::min(hand_evenness[hand], partner);
                }

                auto [held_same_hand, held_total] = press_sweep.held_at(time, press.column);
                DifficultyEvent event = {};
                event[(size_t)EventChannel::TIME] = time;
                event[(size_t)EventChannel::COLUMN] = press.column;
                event[(size_t)EventChannel::HAND] = hand;
                event[(size_t)EventChannel::RELEASE] = 0;
                event[(size_t)EventChannel::HOLD] = press.end ? 1 : 0;
                event[(size_t)EventChannel::HOLD_LENGTH] = press.end ? *press.end - time : 0;
                event[(size_t)EventChannel::COLUMN_GAP] = gap_since(column_last[press.column], time);
                event[(size_t)EventChannel::PREVIOUS_COLUMN_GAP] = column_last_gap[press.column];
                event[(size_t)EventChannel::HAND_GAP] = hand_gap;
                event[(size_t)EventChannel::PREVIOUS_HAND_GAP] = hand_last_gap[hand];
                event[(size_t)EventChannel::ONSET_GAP] = onset_gap;
                event[(size_t)EventChannel::PREVIOUS_ONSET_GAP] = onset_last_gap;
                event[(size_t)EventChannel::CHORD_SIZE] = chord_size;
                event[(size_t)EventChannel::HAND_CHORD_SIZE] = hand_counts[hand];
                event[(size_t)EventChannel::CHORD_OVERLAP] = overlap;
                event[(size_t)EventChannel::HAND_SWITCH] = (previous_hands & (1u << hand)) == 0 && previous_hands != 0 ? 1 : 0;
                event[(size_t)EventChannel::OTHER_HAND_GAP] = other_hand_gap;
                event[(size_t)EventChannel::SNAP] = snap_complexity(time, timing_points);
                event[(size_t)EventChannel::HELD_SAME_HAND] = held_same_hand;
                event[(size_t)EventChannel::HELD_TOTAL] = held_total;
                event[(size_t)EventChannel::NEAREST_HAND_EVENT] = nearest_other(hand_times[hand], time);
                event[(size_t)EventChannel::KEY_COUNT] = key_count;
                event[(size_t)EventChannel::ROLL_EVENNESS] = roll;
                event[(size_t)EventChannel::PATTERN_VARIETY] = variety;
                event[(size_t)EventChannel::JACK_RUN] = jack_runs[index];
                events.push_back({press.id, press.weight, event});
            }

            for (size_t index = first; index < last; index++) {
                uint8_t column = presses[index].column;
                column_last_gap[column] = gap_since(column_last[column], time);
                column_last[column] = time;
            }

            for (size_t hand = 0; hand < MAX_HANDS; hand++) {
                if (hands & (1u << hand)) {
                    hand_last_gap[hand] = gap_since(hand_last[hand], time);
                    hand_last[hand] = time;
                }
            }

            onset_last_gap = onset_gap;
            onset_last = time;
            previous_columns = columns;
            previous_hands = hands;
            first = last;
        }

        std::vector<Press> releases = holds;
        std::stable_sort(releases.begin(), releases.end(), [](const Press& a, const Press& b) {
            return *a.end < *b.end || (*a.end == *b.end && a.column < b.column);
        });

        std::vector<double> release_times;
        std::vector<std::pair<double, uint8_t>> release_hands;

        for (const Press& release : releases) {
            release_times.push_back(*release.end);
            release_hands.push_back({*release.end, difficulty_hand(release.column, key_count)});
        }

        std::sort(release_hands.begin(), release_hands.end());
        HoldSweep release_sweep(holds, key_count);
        std::array<std::vector<double>, MAX_HANDS> hand_press_times;

        for (const Press& press : presses) {
            hand_press_times[difficulty_hand(press.column, key_count)].push_back(press.time);
        }

        for (const Press& press : releases) {
            double time = *press.end;
            uint8_t hand = difficulty_hand(press.column, key_count);
            auto same_time = std::equal_range(release_times.begin(), release_times.end(), time);
            auto same_hand = std::equal_range(release_hands.begin(), release_hands.end(), std::pair<double, uint8_t>{time, hand});
            double hand_releases = (double)std::distance(same_hand.first, same_hand.second);
            auto [held_same_hand, held_total] = release_sweep.held_at(time, press.column);

            DifficultyEvent event = {};
            event[(size_t)EventChannel::TIME] = time;
            event[(size_t)EventChannel::COLUMN] = press.column;
            event[(size_t)EventChannel::HAND] = hand;
            event[(size_t)EventChannel::RELEASE] = 1;
            event[(size_t)EventChannel::HOLD] = 1;
            event[(size_t)EventChannel::HOLD_LENGTH] = time - press.time;
            event[(size_t)EventChannel::COLUMN_GAP] = time - press.time;
            event[(size_t)EventChannel::PREVIOUS_COLUMN_GAP] = NO_PREVIOUS_GAP;
            event[(size_t)EventChannel::HAND_GAP] = previous_before(hand_times[hand], time);
            event[(size_t)EventChannel::PREVIOUS_HAND_GAP] = NO_PREVIOUS_GAP;
            event[(size_t)EventChannel::ONSET_GAP] = previous_before(release_times, time);
            event[(size_t)EventChannel::PREVIOUS_ONSET_GAP] = NO_PREVIOUS_GAP;
            event[(size_t)EventChannel::CHORD_SIZE] = (double)std::distance(same_time.first, same_time.second);
            event[(size_t)EventChannel::HAND_CHORD_SIZE] = hand_releases;
            event[(size_t)EventChannel::CHORD_OVERLAP] = 0;
            event[(size_t)EventChannel::HAND_SWITCH] = 0;
            event[(size_t)EventChannel::OTHER_HAND_GAP] = NO_PREVIOUS_GAP;
            event[(size_t)EventChannel::SNAP] = snap_complexity(time, timing_points);
            event[(size_t)EventChannel::HELD_SAME_HAND] = held_same_hand;
            event[(size_t)EventChannel::HELD_TOTAL] = held_total;
            event[(size_t)EventChannel::NEAREST_HAND_EVENT] = std::binary_search(hand_press_times[hand].begin(), hand_press_times[hand].end(), time) ? 0.0 : nearest_other(hand_times[hand], time);
            event[(size_t)EventChannel::KEY_COUNT] = key_count;
            events.push_back({hits.size() + press.id, press.weight, event});
        }

        return events;
    }

    std::vector<ChartTimingPoint> align_timing(const std::vector<ChartTimingPoint>& timing_points, const std::vector<Hit>& hits) {
        std::vector<ChartTimingPoint> aligned = timing_points;
        std::vector<double> times;
        times.reserve(hits.size());

        for (const Hit& hit : hits) {
            times.push_back((double)hit.time);
        }

        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());

        for (size_t index = 0; index < timing_points.size(); index++) {
            const ChartTimingPoint& point = timing_points[index];

            if (point.beat_length <= 0) {
                continue;
            }

            double start = index == 0 ? -std::numeric_limits<double>::infinity() : (double)point.time - 1;
            double end = index + 1 < timing_points.size() ? (double)timing_points[index + 1].time - 1 : std::numeric_limits<double>::infinity();
            auto first = std::lower_bound(times.begin(), times.end(), start);
            auto last = std::lower_bound(times.begin(), times.end(), end);
            size_t count = (size_t)std::distance(first, last);

            if (count < ALIGN_MIN_NOTES) {
                continue;
            }

            double beat = (double)point.beat_length;
            auto total = [&](double shift, double limit) {
                double sum = 0;

                for (auto it = first; it != last && sum < limit; it++) {
                    sum += grid_snap(*it, (double)point.time + shift, beat);
                }

                return sum;
            };

            double base = total(0.0, std::numeric_limits<double>::infinity());
            double best = base;
            double best_shift = 0.0;
            size_t step = std::max<size_t>(1, count / ALIGN_CANDIDATES);

            for (auto it = first; it < last; it += (std::ptrdiff_t)std::min<size_t>(step, (size_t)std::distance(it, last))) {
                double shift = std::fmod(*it - (double)point.time, beat);

                if (shift < 0) {
                    shift += beat;
                }

                if (shift > beat / 2) {
                    shift -= beat;
                }

                double value = total(shift, best);

                if (value < best) {
                    best = value;
                    best_shift = shift;
                }
            }

            if ((base - best) / (double)count >= ALIGN_MIN_GAIN) {
                aligned[index].time = (int32_t)std::lround((double)point.time + best_shift);
            }
            else {
                best = base;
            }

            if (best / (double)count > UNTIMED_SNAP) {
                aligned[index].beat_length = -1.0f;
            }
        }

        return aligned;
    }

    double active_time_ms(const std::vector<Hit>& hits) {
        std::vector<double> times;
        times.reserve(hits.size() * 2);

        for (const Hit& hit : hits) {
            times.push_back((double)hit.time);

            if (hit.hit_type == HitType::HOLD && hit.end_time > hit.time) {
                times.push_back((double)hit.end_time);
            }
        }

        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());
        double total = 0;

        for (size_t index = 1; index < times.size(); index++) {
            double gap = times[index] - times[index - 1];
            total += gap * std::clamp((ACTIVE_REST_GAP_MS - gap) / (ACTIVE_REST_GAP_MS - ACTIVE_FULL_GAP_MS), 0.0, 1.0);
        }

        return total;
    }

    std::vector<DifficultyEvent> extract_difficulty_events(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points) {
        std::vector<ChartTimingPoint> aligned = align_timing(timing_points, hits);
        std::vector<BuiltEvent> separate = build_events(hits, key_count, aligned, 0.0);
        std::vector<BuiltEvent> merged = build_events(hits, key_count, aligned, CHORD_SEPARATE_MS);
        std::vector<const BuiltEvent*> by_id(2 * hits.size(), nullptr);

        for (const BuiltEvent& built : merged) {
            by_id[built.id] = &built;
        }

        std::vector<DifficultyEvent> events;
        events.reserve(separate.size());

        for (const BuiltEvent& built : separate) {
            const BuiltEvent* other = by_id[built.id];
            events.push_back(other && other->weight > 0 ? blend_events(built.event, other->event, other->weight) : built.event);
        }

        std::stable_sort(events.begin(), events.end(), [](const DifficultyEvent& a, const DifficultyEvent& b) {
            double ta = a[(size_t)EventChannel::TIME];
            double tb = b[(size_t)EventChannel::TIME];

            if (ta != tb) {
                return ta < tb;
            }

            if (a[(size_t)EventChannel::RELEASE] != b[(size_t)EventChannel::RELEASE]) {
                return a[(size_t)EventChannel::RELEASE] > b[(size_t)EventChannel::RELEASE];
            }

            return a[(size_t)EventChannel::COLUMN] < b[(size_t)EventChannel::COLUMN];
        });

        return events;
    }
}
