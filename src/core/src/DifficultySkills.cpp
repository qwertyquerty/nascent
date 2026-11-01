#include "DifficultySkills.h"
#include "Accuracy.h"
#include "DifficultyCalculator.h"
#include "DifficultyEvents.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>

namespace nascent {
    namespace {
        constexpr size_t PATTERN_COUNT = 5;
        constexpr std::array<DifficultySkill, PATTERN_COUNT> PATTERN_SKILLS = {DifficultySkill::STREAM, DifficultySkill::CHORDSTREAM, DifficultySkill::JACK, DifficultySkill::CHORDJACK, DifficultySkill::VIBRO};
        constexpr size_t CONTEXT_GROUPS = 4;
        constexpr size_t TECH_GROUPS = 16;
        constexpr double JACK_RATIO = 1.5;
        constexpr double SPLIT_JACK_RUN_START = 2.0;
        constexpr double SPLIT_JACK_RUN_FULL = 4.0;
        constexpr double JACK_SHARE_START = 0.15;
        constexpr double JACK_SHARE_FULL = 0.55;
        constexpr double JACK_SHARE_CHORD = 1.5;
        constexpr double CHORDSTREAM_START = 1.1;
        constexpr double CHORDSTREAM_FULL = 1.5;
        constexpr double CHORDJACK_START = 1.2;
        constexpr double CHORDJACK_FULL = 1.8;
        constexpr double VIBRO_SLOW_MS = 120.0;
        constexpr double VIBRO_FAST_MS = 90.0;
        constexpr double VIBRO_RUN_START = 2.0;
        constexpr double VIBRO_RUN_FULL = 5.0;
        constexpr double RHYTHM_ACTIVE_GAP_MS = 500.0;
        constexpr double SNAP_PLAIN = 0.1;
        constexpr double TECH_MIX_WEIGHT = 1.0;
        constexpr double TECH_RHYTHM_WEIGHT = 1.0;
        constexpr double TECH_SNAP_WEIGHT = 1.0;
        constexpr double TECH_START = 0.3;
        constexpr double TECH_FULL = 0.7;
        constexpr double SUSTAIN_BIN_MS = 5000.0;
        constexpr double STAMINA_START_NOTES = 300.0;
        constexpr double STAMINA_FULL_NOTES = 3500.0;
        constexpr double STAMINA_SPAN = 2.0;
        constexpr double STAMINA_MAX_SHARE = 0.5;

        using Labels = std::array<double, DIFFICULTY_SKILL_COUNT>;
        using Pattern = std::array<double, PATTERN_COUNT>;

        struct RowFeatures {
            double held = 0;
            double chord = 0;
            double jack = 0;
            double vibro = 0;
            double rhythm = 0;
            double snap = 0;
        };

        double channel(const DifficultyEvent& event, EventChannel name) {
            return event[(size_t)name];
        }

        double ramp(double value, double start, double full) {
            return std::clamp((value - start) / (full - start), 0.0, 1.0);
        }

        RowFeatures row_features(const std::vector<DifficultyEvent>& events, const DifficultyGroup& group) {
            RowFeatures row;
            double count = (double)(group.last - group.first);

            for (size_t index = group.first; index < group.last; index++) {
                const DifficultyEvent& event = events[index];
                double column = channel(event, EventChannel::COLUMN_GAP);
                double onset = channel(event, EventChannel::ONSET_GAP);
                double hand = channel(event, EventChannel::HAND_GAP);
                double run = channel(event, EventChannel::JACK_RUN);
                double previous = channel(event, EventChannel::PREVIOUS_ONSET_GAP);
                double next_row = column < NO_PREVIOUS_GAP && onset < NO_PREVIOUS_GAP ? ramp(column / std::max(onset, 1.0), JACK_RATIO, 1.0) : 0.0;
                double split = column < NO_PREVIOUS_GAP && hand < NO_PREVIOUS_GAP ? ramp(column / std::max(hand, 1.0), JACK_RATIO, 1.0) * ramp(run, SPLIT_JACK_RUN_START, SPLIT_JACK_RUN_FULL) : 0.0;
                double jack = std::max(next_row, split);
                double fast = ramp(column, VIBRO_SLOW_MS, VIBRO_FAST_MS) * ramp(channel(event, EventChannel::PREVIOUS_COLUMN_GAP), VIBRO_SLOW_MS, VIBRO_FAST_MS);
                bool held = channel(event, EventChannel::HOLD) > 0.5 || channel(event, EventChannel::HELD_SAME_HAND) > 0;
                bool active = onset < RHYTHM_ACTIVE_GAP_MS && previous < RHYTHM_ACTIVE_GAP_MS;

                row.held += (held ? 1.0 : 0.0) / count;
                row.chord += channel(event, EventChannel::CHORD_SIZE) / count;
                row.jack += jack / count;
                row.vibro += jack * fast * ramp(run, VIBRO_RUN_START, VIBRO_RUN_FULL) / count;
                row.rhythm += (active ? std::min(1.0, std::abs(std::log2(std::max(onset, 1.0) / std::max(previous, 1.0)))) : 0.0) / count;
                row.snap += ramp(channel(event, EventChannel::SNAP), SNAP_PLAIN, 1.0) / count;
            }

            return row;
        }

        template <typename Value>
        double window_mean(const std::vector<RowFeatures>& rows, size_t centre, size_t radius, Value&& value) {
            size_t first = centre >= radius ? centre - radius : 0;
            size_t last = std::min(rows.size(), centre + radius + 1);
            double total = 0;

            for (size_t index = first; index < last; index++) {
                total += value(rows[index]);
            }

            return total / (double)(last - first);
        }

        Pattern pattern_shares(const std::vector<RowFeatures>& rows, size_t centre) {
            double chord = window_mean(rows, centre, CONTEXT_GROUPS, [](const RowFeatures& row) { return row.chord; });
            double jack_mean = window_mean(rows, centre, CONTEXT_GROUPS, [](const RowFeatures& row) { return row.jack; });
            double vibro_mean = window_mean(rows, centre, CONTEXT_GROUPS, [](const RowFeatures& row) { return row.vibro; });
            double density = std::sqrt(JACK_SHARE_CHORD / std::max(chord, JACK_SHARE_CHORD));
            double jack = ramp(jack_mean, JACK_SHARE_START * density, JACK_SHARE_FULL * density);
            double vibro = jack_mean > 0 ? jack * std::min(vibro_mean / jack_mean, 1.0) : 0.0;
            double chordstream = ramp(chord, CHORDSTREAM_START, CHORDSTREAM_FULL);
            double chordjack = ramp(chord, CHORDJACK_START, CHORDJACK_FULL);
            return {(1.0 - jack) * (1.0 - chordstream), (1.0 - jack) * chordstream, (jack - vibro) * (1.0 - chordjack), (jack - vibro) * chordjack, vibro};
        }

        double gini(const Pattern& pattern) {
            double sum = 0;

            for (double share : pattern) {
                sum += share * share;
            }

            return 1.0 - sum;
        }

        double technical_share(const std::vector<Pattern>& patterns, const std::vector<RowFeatures>& rows, size_t centre) {
            size_t first = centre >= TECH_GROUPS ? centre - TECH_GROUPS : 0;
            size_t last = std::min(rows.size(), centre + TECH_GROUPS + 1);
            double count = (double)(last - first);
            Pattern mean = {};
            double impurity = 0, rhythm = 0, snap = 0;

            for (size_t index = first; index < last; index++) {
                for (size_t part = 0; part < PATTERN_COUNT; part++) {
                    mean[part] += patterns[index][part] / count;
                }

                impurity += gini(patterns[index]) / count;
                rhythm += rows[index].rhythm / count;
                snap += rows[index].snap / count;
            }

            double mixing = std::max(0.0, gini(mean) - impurity) / (1.0 - 1.0 / (double)PATTERN_COUNT);
            return ramp(TECH_MIX_WEIGHT * mixing + TECH_RHYTHM_WEIGHT * rhythm + TECH_SNAP_WEIGHT * snap, TECH_START, TECH_FULL);
        }

        std::vector<Labels> group_labels(const DifficultyAnalysis& analysis) {
            std::vector<Labels> labels(analysis.groups.size(), Labels{});
            std::vector<size_t> presses;
            std::vector<RowFeatures> rows;

            for (size_t index = 0; index < analysis.groups.size(); index++) {
                if (analysis.groups[index].release) {
                    labels[index][(size_t)DifficultySkill::LONG_NOTES] = 1.0;
                    continue;
                }

                presses.push_back(index);
                rows.push_back(row_features(analysis.events, analysis.groups[index]));
            }

            std::vector<Pattern> patterns(rows.size());

            for (size_t index = 0; index < rows.size(); index++) {
                patterns[index] = pattern_shares(rows, index);
            }

            for (size_t index = 0; index < rows.size(); index++) {
                Labels& label = labels[presses[index]];
                double held = rows[index].held;
                double technical = technical_share(patterns, rows, index);
                double rest = (1.0 - held) * (1.0 - technical);
                label[(size_t)DifficultySkill::LONG_NOTES] = held;
                label[(size_t)DifficultySkill::TECHNICAL] = technical * (1.0 - held);

                for (size_t part = 0; part < PATTERN_COUNT; part++) {
                    label[(size_t)PATTERN_SKILLS[part]] = rest * patterns[index][part];
                }
            }

            return labels;
        }

        double effective_count(const std::map<int64_t, double>& weights) {
            double total = 0;

            for (const auto& [key, weight] : weights) {
                total += weight;
            }

            if (total <= 0) {
                return 0.0;
            }

            double entropy = 0;

            for (const auto& [key, weight] : weights) {
                double share = weight / total;
                entropy -= share > 0 ? share * std::log(share) : 0.0;
            }

            return std::exp(entropy);
        }

        double sustained_seconds(const DifficultyAnalysis& analysis) {
            std::map<int64_t, double> bins;

            for (size_t index = 0; index < analysis.groups.size(); index++) {
                double time = channel(analysis.events[analysis.groups[index].first], EventChannel::TIME);
                bins[(int64_t)std::floor(time / SUSTAIN_BIN_MS)] += analysis.importance[index];
            }

            return effective_count(bins) * SUSTAIN_BIN_MS / 1000.0;
        }

        double sustained_notes(const DifficultyAnalysis& analysis) {
            std::map<int64_t, double> groups;
            double weighted_size = 0, total = 0;

            for (size_t index = 0; index < analysis.groups.size(); index++) {
                const DifficultyGroup& group = analysis.groups[index];

                if (group.release) {
                    continue;
                }

                double weight = analysis.importance[index];
                groups[(int64_t)index] = weight;
                weighted_size += weight * (double)(group.last - group.first);
                total += weight;
            }

            return total > 0 ? effective_count(groups) * weighted_size / total : 0.0;
        }
    }

    DifficultyBreakdown DifficultySkills::breakdown(const Chart& chart) {
        return breakdown(chart.hits, chart.info.key_count, chart.timing_points, RANKED_HIT_ACC_DIFFICULTY);
    }

    DifficultyBreakdown DifficultySkills::breakdown_at_pitch(const Chart& chart, float pitch) {
        std::vector<Hit> hits;
        std::vector<ChartTimingPoint> timing_points;
        chart.pitched_timing(pitch, hits, timing_points);
        return breakdown(hits, chart.unpitched_key_count(), timing_points, RANKED_HIT_ACC_DIFFICULTY);
    }

    DifficultyBreakdown DifficultySkills::breakdown(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy) {
        DifficultyBreakdown result;

        if (hits.empty() || key_count == 0) {
            return result;
        }

        std::optional<DifficultyAnalysis> analysis = DifficultyCalculator::analyse(hits, key_count, timing_points, hit_accuracy);

        if (!analysis) {
            result.rating = DifficultyCalculator::empty_rating(hits.size(), hit_accuracy);
            return result;
        }

        result.rating = analysis->rating;
        std::vector<Labels> labels = group_labels(*analysis);
        double total = 0;

        for (size_t index = 0; index < labels.size(); index++) {
            for (size_t part = 0; part < DIFFICULTY_SKILL_COUNT; part++) {
                result.mix[part] += analysis->importance[index] * labels[index][part];
            }

            total += analysis->importance[index];
        }

        result.sustained_seconds = sustained_seconds(*analysis);
        result.sustained_notes = sustained_notes(*analysis);
        double stamina = ramp(std::log(std::max(result.sustained_notes, 1.0)), std::log(STAMINA_START_NOTES), std::log(STAMINA_FULL_NOTES));

        for (size_t part = 0; part < DIFFICULTY_SKILL_COUNT; part++) {
            result.mix[part] = total > 0 ? result.mix[part] / total * (1.0 - stamina * STAMINA_MAX_SHARE) : 0.0;
        }

        result.mix[(size_t)DifficultySkill::STAMINA] += stamina * STAMINA_MAX_SHARE;
        std::array<std::optional<double>, DIFFICULTY_SKILL_COUNT> parts = {};
        std::vector<double> shares(labels.size());

        for (size_t part = 0; part < DIFFICULTY_SKILL_COUNT; part++) {
            if (part == (size_t)DifficultySkill::STAMINA) {
                parts[part] = analysis->skill - STAMINA_SPAN * (1.0 - stamina);
                continue;
            }

            if (result.mix[part] <= 0) {
                continue;
            }

            for (size_t index = 0; index < labels.size(); index++) {
                shares[index] = labels[index][part];
            }

            parts[part] = analysis->part_skill(shares);
        }

        double top = *parts[(size_t)DifficultySkill::STAMINA];

        for (const std::optional<double>& part : parts) {
            top = part ? std::max(top, *part) : top;
        }

        for (size_t part = 0; part < DIFFICULTY_SKILL_COUNT; part++) {
            result.ratings[part] = parts[part] ? analysis->displayed(analysis->skill + (*parts[part] - top)) : 0.0;
        }

        return result;
    }
}
