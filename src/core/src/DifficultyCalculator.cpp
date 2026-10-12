#include "DifficultyCalculator.h"
#include "Accuracy.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace nascent {
    namespace {
        constexpr double WINDOW = 1000.0;
        constexpr double STEP = 250.0;
        constexpr double MIN_RATE_INTERVAL = 30.0;
        constexpr double MIN_BURST_GAP = 25.0;
        constexpr std::array<int, 10> SNAP_DIVISORS = {1, 2, 4, 3, 8, 6, 12, 16, 24, 32};

        struct Note {
            double time;
            uint8_t column;
            std::optional<double> end;
        };

        struct Window {
            std::vector<double> columns;
            std::vector<double> busy_columns;
            std::vector<double> release_columns;
            std::vector<double> hand_actions;
            std::vector<double> hand_chord_extra;
            double onsets = 0;
            double irregularity = 0;
            double hand_speed = 0;
            double hand_burst = 0;
            double finger_speed = 0;
            double snap = 0;
            double burst = 0;
            double variety = 0;
            double tempo = 0;
            double coverage = 0;
        };

        uint8_t hand_of(uint8_t column, uint8_t keys) {
            uint8_t half = keys / 2;

            if (keys % 2 == 1 && column == half) {
                return 2;
            }

            return column < half ? 0 : 1;
        }

        double snap_value(int divisor) {
            switch (divisor) {
                case 1:
                case 2:
                    return 0.0;
                case 4:
                    return 0.1;
                case 8:
                    return 0.3;
                case 3:
                    return 0.5;
                case 6:
                    return 0.6;
                case 12:
                    return 0.8;
                case 16:
                    return 0.7;
                default:
                    return 0.9;
            }
        }

        double snap_complexity(double time, const std::vector<ChartTimingPoint>& timing_points) {
            if (timing_points.empty()) {
                return 0.0;
            }

            auto after = std::partition_point(timing_points.begin(), timing_points.end(), [time](const ChartTimingPoint& tp) {
                return tp.time <= time + 1;
            });
            const ChartTimingPoint* point = after == timing_points.begin() ? &timing_points.front() : &*std::prev(after);

            double beats = (time - point->time) / point->beat_length;
            double fraction = beats - std::floor(beats);
            double tolerance = 3.0 / point->beat_length;

            for (int divisor : SNAP_DIVISORS) {
                double position = fraction * divisor;

                if (std::abs(position - std::round(position)) * (1.0 / divisor) < tolerance) {
                    return snap_value(divisor);
                }
            }

            return 1.0;
        }

        double power_mean(const std::vector<double>& values, double power) {
            if (values.empty()) {
                return 0.0;
            }

            double total = 0.0;

            for (double value : values) {
                total += std::pow(value, power);
            }

            return std::pow(total / values.size(), 1.0 / power);
        }

        double median_of_smallest(std::vector<double> values, size_t count) {
            size_t kept = std::min(count, values.size());
            std::partial_sort(values.begin(), values.begin() + (std::ptrdiff_t)kept, values.end());
            values.resize(kept);

            size_t middle = values.size() / 2;
            return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0;
        }
    }

    double DifficultyCalculator::calculate(const Chart& chart, const DifficultyParameters& parameters) {
        return calculate(chart.hits, chart.info.key_count, chart.timing_points, RANKED_HIT_ACC_DIFFICULTY, parameters);
    }

    double DifficultyCalculator::calculate_at_pitch(const Chart& chart, float pitch, const DifficultyParameters& parameters) {
        std::vector<Hit> hits;
        std::vector<ChartTimingPoint> timing_points;
        chart.pitched_timing(pitch, hits, timing_points);
        return calculate(hits, chart.unpitched_key_count(), timing_points, RANKED_HIT_ACC_DIFFICULTY, parameters);
    }

    double DifficultyCalculator::strictness_factor(uint8_t hit_accuracy, const DifficultyParameters& p) {
        double accuracy = std::clamp<double>(hit_accuracy, MIN_HIT_ACC_DIFFICULTY, MAX_HIT_ACC_DIFFICULTY);
        double reference = std::clamp<double>(p.strictness_reference, MIN_HIT_ACC_DIFFICULTY, MAX_HIT_ACC_DIFFICULTY);
        double widest = 2.0 * MAX_HIT_ACC_DIFFICULTY;
        return std::pow((widest - reference) / (widest - accuracy), p.strictness_exponent);
    }

    double DifficultyCalculator::calculate(const std::vector<Hit>& hits, uint8_t keys, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy, const DifficultyParameters& p) {
        if (hits.empty() || keys == 0) {
            return 0.0;
        }

        const uint8_t hand_count = keys % 2 == 1 ? 3 : 2;

        std::vector<Note> notes;
        notes.reserve(hits.size());

        for (const Hit& hit : hits) {
            bool is_hold = hit.hit_type == HitType::HOLD && hit.end_time > hit.time;
            notes.push_back({(double)hit.time, hit.key, is_hold ? std::optional<double>(hit.end_time) : std::nullopt});
        }

        std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
            return a.time < b.time || (a.time == b.time && a.column < b.column);
        });

        std::vector<std::pair<double, double>> holds;
        std::vector<std::pair<double, uint8_t>> releases;
        std::vector<std::pair<std::pair<double, double>, uint8_t>> holds_by_start;

        for (const Note& note : notes) {
            if (note.end) {
                holds.push_back({note.time, *note.end});
                releases.push_back({*note.end, note.column});
                holds_by_start.push_back({{note.time, *note.end}, note.column});
            }
        }

        std::stable_sort(releases.begin(), releases.end(), [](const auto& a, const auto& b) {
            return a.first < b.first || (a.first == b.first && a.second < b.second);
        });

        std::vector<bool> busy(notes.size(), false);
        std::vector<std::optional<std::pair<double, double>>> active_hold(keys);
        size_t next_hold = 0;

        for (size_t i = 0; i < notes.size(); i++) {
            const Note& note = notes[i];

            while (next_hold < holds_by_start.size() && holds_by_start[next_hold].first.first < note.time) {
                active_hold[holds_by_start[next_hold].second] = holds_by_start[next_hold].first;
                next_hold++;
            }

            uint8_t hand = hand_of(note.column, keys);

            for (uint8_t column = 0; column < keys; column++) {
                if (column == note.column || hand_of(column, keys) != hand || !active_hold[column]) {
                    continue;
                }

                if (active_hold[column]->first < note.time && note.time < active_hold[column]->second) {
                    busy[i] = true;
                    break;
                }
            }
        }

        std::vector<std::optional<double>> hand_rate(notes.size());
        std::vector<double> finger_rate(notes.size());
        std::vector<double> note_snap(notes.size());
        std::vector<std::optional<double>> hand_last(hand_count);
        std::vector<std::optional<double>> finger_last(keys);

        for (size_t i = 0; i < notes.size(); i++) {
            const Note& note = notes[i];
            uint8_t hand = hand_of(note.column, keys);

            if (hand_last[hand] && note.time > *hand_last[hand]) {
                hand_rate[i] = 1000.0 / std::max(note.time - *hand_last[hand], MIN_RATE_INTERVAL);
            }
            else if (hand_last[hand] && note.time == *hand_last[hand]) {
                hand_rate[i] = std::nullopt;
            }
            else {
                hand_rate[i] = 1.0;
            }

            hand_last[hand] = note.time;

            finger_rate[i] = finger_last[note.column] ? 1000.0 / std::max(note.time - *finger_last[note.column], MIN_RATE_INTERVAL) : 1.0;
            finger_last[note.column] = note.time;

            note_snap[i] = snap_complexity(note.time, timing_points);
        }

        double start = notes.front().time;
        double end = start;

        for (const Note& note : notes) {
            end = std::max(end, note.time);
        }

        std::vector<double> note_times(notes.size());
        std::transform(notes.begin(), notes.end(), note_times.begin(), [](const Note& n) { return n.time; });

        std::vector<double> release_times(releases.size());
        std::transform(releases.begin(), releases.end(), release_times.begin(), [](const auto& r) { return r.first; });

        std::vector<double> timing_point_times(timing_points.size());
        std::transform(timing_points.begin(), timing_points.end(), timing_point_times.begin(), [](const ChartTimingPoint& tp) { return (double)tp.time; });

        const double fatigue_alpha = 1.0 - std::exp(-(STEP / 1000.0) / p.fatigue_time);
        const double scale = 1000.0 / WINDOW;

        double fatigue = 0.0;
        double aggregate = 0.0;
        size_t window_count = 0;

        std::vector<double> onset_times;
        std::vector<double> gaps;
        std::vector<double> buckets;
        std::vector<double> columns(keys);
        std::vector<double> actions(hand_count);
        std::vector<size_t> active_holds;
        size_t next_coverage_hold = 0;

        Window window;
        window.columns.resize(keys);
        window.busy_columns.resize(keys);
        window.release_columns.resize(keys);
        window.hand_actions.resize(hand_count);
        window.hand_chord_extra.resize(hand_count);

        for (size_t step = 0;; step++) {
            double window_start = start + step * STEP;

            if (window_start > end + 1e-9) {
                break;
            }

            double window_end = window_start + WINDOW;

            std::fill(window.columns.begin(), window.columns.end(), 0.0);
            std::fill(window.busy_columns.begin(), window.busy_columns.end(), 0.0);
            std::fill(window.release_columns.begin(), window.release_columns.end(), 0.0);
            std::fill(window.hand_actions.begin(), window.hand_actions.end(), 0.0);
            std::fill(window.hand_chord_extra.begin(), window.hand_chord_extra.end(), 0.0);
            window.onsets = window.irregularity = window.hand_speed = window.hand_burst = 0.0;
            window.finger_speed = window.snap = window.burst = window.variety = window.tempo = window.coverage = 0.0;

            size_t first = std::lower_bound(note_times.begin(), note_times.end(), window_start) - note_times.begin();
            size_t last = std::lower_bound(note_times.begin(), note_times.end(), window_end) - note_times.begin();

            onset_times.clear();
            double hand_rate_total = 0.0;
            size_t hand_rate_count = 0;
            double snap_total = 0.0;

            for (size_t i = first; i < last; i++) {
                const Note& note = notes[i];
                window.columns[note.column] += 1;

                if (busy[i]) {
                    window.busy_columns[note.column] += 1;
                }

                uint8_t hand = hand_of(note.column, keys);

                if (i > first && notes[i - 1].time == note.time && hand_of(notes[i - 1].column, keys) == hand) {
                    window.hand_chord_extra[hand] += 1;
                }
                else {
                    window.hand_actions[hand] += 1;
                }

                if (onset_times.empty() || onset_times.back() != note.time) {
                    onset_times.push_back(note.time);
                }

                if (hand_rate[i]) {
                    hand_rate_total += *hand_rate[i];
                    hand_rate_count++;
                    window.hand_burst = std::max(window.hand_burst, *hand_rate[i]);
                }

                window.finger_speed = std::max(window.finger_speed, finger_rate[i]);
                snap_total += note_snap[i];
            }

            window.onsets = (double)onset_times.size();

            if (onset_times.size() >= 2) {
                gaps.clear();

                for (size_t i = 1; i < onset_times.size(); i++) {
                    gaps.push_back(std::max(onset_times[i] - onset_times[i - 1], MIN_BURST_GAP));
                }

                window.burst = 1000.0 / median_of_smallest(gaps, 3);

                buckets.clear();

                for (double gap : gaps) {
                    buckets.push_back(std::nearbyint(gap / 10.0));
                }

                std::sort(buckets.begin(), buckets.end());
                window.variety = (double)(std::unique(buckets.begin(), buckets.end()) - buckets.begin());
            }

            window.tempo = (double)(std::lower_bound(timing_point_times.begin(), timing_point_times.end(), window_end) - std::lower_bound(timing_point_times.begin(), timing_point_times.end(), window_start));

            if (onset_times.size() >= 3) {
                gaps.clear();

                for (size_t i = 1; i < onset_times.size(); i++) {
                    double gap = onset_times[i] - onset_times[i - 1];

                    if (gap > 0) {
                        gaps.push_back(gap);
                    }
                }

                if (gaps.size() >= 2) {
                    double total = 0.0;

                    for (size_t i = 1; i < gaps.size(); i++) {
                        total += std::abs(std::log2(gaps[i] / gaps[i - 1]));
                    }

                    window.irregularity = std::min(total / (gaps.size() - 1), 2.0);
                }
            }

            if (hand_rate_count > 0) {
                window.hand_speed = hand_rate_total / hand_rate_count;
            }

            if (last > first) {
                window.snap = snap_total / (last - first);
            }
            else {
                window.finger_speed = 0.0;
            }

            size_t release_first = std::lower_bound(release_times.begin(), release_times.end(), window_start) - release_times.begin();
            size_t release_last = std::lower_bound(release_times.begin(), release_times.end(), window_end) - release_times.begin();

            for (size_t i = release_first; i < release_last; i++) {
                window.release_columns[releases[i].second] += 1;
            }

            if (p.hold_coverage_weight != 0.0) {
                double covered = 0.0;

                while (next_coverage_hold < holds.size() && holds[next_coverage_hold].first < window_end) {
                    active_holds.push_back(next_coverage_hold++);
                }

                std::erase_if(active_holds, [&](size_t index) {
                    return holds[index].second <= window_start;
                });

                for (size_t index : active_holds) {
                    const auto& [hold_start, hold_end] = holds[index];
                    covered += std::max(std::min(hold_end, window_end) - std::max(hold_start, window_start), 0.0);
                }

                window.coverage = covered / WINDOW;
            }

            for (uint8_t column = 0; column < keys; column++) {
                columns[column] = window.columns[column] + p.hold_busy_weight * window.busy_columns[column] + p.release_weight * window.release_columns[column];
            }

            for (uint8_t hand = 0; hand < hand_count; hand++) {
                actions[hand] = window.hand_actions[hand] + p.chord_bonus * window.hand_chord_extra[hand];
            }

            double finger = power_mean(columns, p.finger_power) * keys;
            double hand = power_mean(actions, p.hand_power) * hand_count;

            double load = (p.finger_weight * finger + p.hand_weight * hand + p.onset_weight * window.onsets) * scale;
            load += p.hand_speed_weight * window.hand_speed + p.hand_burst_weight * window.hand_burst + p.finger_speed_weight * window.finger_speed;
            load += p.burst_weight * window.burst + p.hold_coverage_weight * window.coverage;
            load *= 1.0 + p.rhythm_weight * window.irregularity + p.snap_weight * window.snap + p.variety_weight * window.variety / 10.0 + p.tempo_weight * std::min(window.tempo, 4.0);

            fatigue = fatigue_alpha * load + (1.0 - fatigue_alpha) * fatigue;
            double effective = (1.0 - p.fatigue_blend) * load + p.fatigue_blend * fatigue;

            aggregate += std::pow(effective, p.aggregate_power);
            window_count++;
        }

        if (window_count == 0) {
            return 0.0;
        }

        return std::pow(aggregate / window_count, 1.0 / p.aggregate_power) * p.rating_scale * strictness_factor(hit_accuracy, p);
    }
}
