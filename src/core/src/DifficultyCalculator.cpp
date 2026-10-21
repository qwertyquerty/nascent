#include "DifficultyCalculator.h"
#include "Accuracy.h"
#include "DifficultyBasis.h"
#include "DifficultyEvents.h"
#include "DifficultyModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace nascent {
    namespace {
        constexpr size_t GRID_SIZE = 73;
        constexpr double GRID_LOW = -8.0;
        constexpr double GRID_HIGH = 10.0;
        constexpr double STRAIN_EPSILON = 1e-3;
        constexpr double MIN_COVARIATE_NOTES = 200.0;
        constexpr double MAX_COVARIATE_NOTES = 30000.0;
        constexpr double MIN_COVARIATE_LENGTH_MS = 1000.0;
        constexpr double MAX_COVARIATE_LENGTH_MS = 1800000.0;
        constexpr double STRICTNESS_REFERENCE = 5.0;
        constexpr size_t START_POINT = 32;
        constexpr size_t MAX_WALK_STEPS = 6;
        constexpr double WALK_FALLBACK = 4.0;

        struct Model {
            std::vector<size_t> features;
            std::array<std::vector<double>, 4> weights;
            BasisMask mask;
        };

        struct Group {
            double weak;
            double strong;
            uint8_t kind;
            double notes;
        };

        double lookup(std::span<const difficulty_model::NamedWeight> table, std::string_view name, double fallback) {
            for (const difficulty_model::NamedWeight& entry : table) {
                if (entry.name == name) {
                    return entry.value;
                }
            }

            return fallback;
        }

        Model build_model(uint8_t key_count) {
            std::unordered_map<std::string, size_t> positions;

            for (size_t index = 0; index < BASIS_SIZE; index++) {
                positions.emplace(basis_name(index), index);
            }

            std::span<const difficulty_model::NamedWeight> mode;

            if (key_count == 4) {
                mode = difficulty_model::MODE_4_FACTORS;
            }
            else if (key_count == 7) {
                mode = difficulty_model::MODE_7_FACTORS;
            }

            Model model;

            for (const difficulty_model::NamedWeight& entry : difficulty_model::WEIGHTS) {
                auto found = positions.find(std::string(entry.name));

                if (found == positions.end()) {
                    continue;
                }

                double weight = entry.value * lookup(mode, entry.name, 1.0);
                double strong = lookup(difficulty_model::STRONG_FACTORS, entry.name, 1.0);
                double release = lookup(difficulty_model::RELEASE_FACTORS, entry.name, 1.0);
                model.features.push_back(found->second);
                model.mask.set(found->second);
                model.weights[0].push_back(weight);
                model.weights[1].push_back(weight * strong);
                model.weights[2].push_back(weight * release);
                model.weights[3].push_back(weight * strong * release);
            }

            return model;
        }

        const Model& model_for(uint8_t key_count) {
            static const Model four = build_model(4);
            static const Model seven = build_model(7);
            static const Model other = build_model(0);
            return key_count == 4 ? four : key_count == 7 ? seven
                                                          : other;
        }

        double log_sigmoid(double x) {
            return x >= 0 ? -std::log1p(std::exp(-x)) : x - std::log1p(std::exp(x));
        }

        double skill_rating(const std::vector<Hit>& sorted_hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points) {
            std::vector<DifficultyEvent> events = extract_difficulty_events(sorted_hits, key_count, timing_points);

            if (events.empty()) {
                return GRID_LOW;
            }

            const Model& model = model_for(key_count);
            std::vector<Group> groups;
            BasisAccumulator accumulator(model.mask);

            for (size_t first = 0; first < events.size();) {
                size_t last = first;
                const BasisVector* features = nullptr;

                while (last < events.size() && basis_same_group(events[first], events[last])) {
                    features = &accumulator.push(events[last]);
                    last++;
                }

                uint8_t kind = events[first][(size_t)EventChannel::RELEASE] > 0.5 ? 1 : 0;
                const std::vector<double>& weak_weights = model.weights[kind * 2];
                const std::vector<double>& strong_weights = model.weights[kind * 2 + 1];
                double a = 0, b = 0;

                for (size_t index = 0; index < model.features.size(); index++) {
                    double value = (*features)[model.features[index]];
                    a += weak_weights[index] * value;
                    b += strong_weights[index] * value;
                }

                groups.push_back({a, b, kind, (double)(last - first)});
                first = last;
            }

            double presses = (double)sorted_hits.size();
            double holds = (double)std::count_if(sorted_hits.begin(), sorted_hits.end(), [](const Hit& hit) {
                return hit.hit_type == HitType::HOLD && hit.end_time > hit.time;
            });
            double length = active_time_ms(sorted_hits);
            double shift = difficulty_model::SLOPE_NOTES * std::log(std::clamp(presses, MIN_COVARIATE_NOTES, MAX_COVARIATE_NOTES) / 1000.0) +
                difficulty_model::SLOPE_LENGTH * std::log(std::clamp(length, MIN_COVARIATE_LENGTH_MS, MAX_COVARIATE_LENGTH_MS) / 120000.0) +
                difficulty_model::SLOPE_HOLDS * holds / std::max(presses, 1.0);
            double target = std::log(1.0 - difficulty_model::TARGET_ACCURACY);

            auto grid = [](size_t point) {
                return GRID_LOW + (GRID_HIGH - GRID_LOW) * (double)point / (double)(GRID_SIZE - 1);
            };

            auto curve = [&](size_t point) {
                double skill = grid(point);
                double mix = 0.5 * (1.0 + std::tanh(0.5 * (skill - difficulty_model::SKILL_CENTER) / difficulty_model::SKILL_WIDTH));
                double total = 0;

                for (const Group& group : groups) {
                    const difficulty_model::NoteCurve& c = difficulty_model::CURVES[group.kind];
                    double strain = std::log((1 - mix) * group.weak + mix * group.strong + STRAIN_EPSILON) + shift + (group.kind ? difficulty_model::RELEASE_OFFSET : 0.0);
                    double loss = c.floor + c.span * std::exp(c.shape * log_sigmoid(c.steepness * (strain - skill)));
                    total += group.notes * std::pow(loss, difficulty_model::POWER);
                }

                return std::log(total / (double)events.size()) / difficulty_model::POWER;
            };

            std::array<double, GRID_SIZE> values;
            std::array<bool, GRID_SIZE> known = {};

            auto value = [&](size_t point) {
                if (!known[point]) {
                    values[point] = curve(point);
                    known[point] = true;
                }

                return values[point];
            };

            size_t below = START_POINT;

            for (size_t step = 0; step < MAX_WALK_STEPS; step++) {
                double low = value(below), high = value(below + 1);

                if (low > target && high <= target) {
                    break;
                }

                if (low <= target && below == 0) {
                    return grid(0);
                }

                if (high > target && below + 2 == GRID_SIZE) {
                    return grid(GRID_SIZE - 1);
                }

                double slope = high - low;
                double estimate = slope < -1e-12 ? (double)below + (target - low) / slope : (low <= target ? (double)below - WALK_FALLBACK : (double)below + WALK_FALLBACK);

                if (low <= target) {
                    below = (size_t)std::clamp(std::floor(estimate), 0.0, (double)below - 1.0);
                }
                else {
                    below = (size_t)std::clamp(std::floor(estimate), (double)below + 1.0, (double)(GRID_SIZE - 2));
                }
            }

            if (!(value(below) > target && value(below + 1) <= target)) {
                if (value(0) <= target) {
                    return grid(0);
                }

                if (value(GRID_SIZE - 1) > target) {
                    return grid(GRID_SIZE - 1);
                }

                size_t low = 0, high = GRID_SIZE - 1;

                while (high - low > 1) {
                    size_t middle = (low + high) / 2;
                    (value(middle) <= target ? high : low) = middle;
                }

                below = low;
            }

            double below_value = value(below), above_value = value(below + 1);
            double fraction = std::clamp((below_value - target) / std::max(below_value - above_value, 1e-9), 0.0, 1.0);
            return grid(below) + fraction * (grid(below + 1) - grid(below));
        }

        double displayed(double rating) {
            double position = std::max(0.0, (rating - difficulty_model::DISPLAY_FLOOR) / (difficulty_model::DISPLAY_ANCHOR - difficulty_model::DISPLAY_FLOOR));
            double below = 1.0 - std::min(position, 1.0);
            double power = difficulty_model::DISPLAY_HIGH_POWER + (difficulty_model::DISPLAY_LOW_POWER - difficulty_model::DISPLAY_HIGH_POWER) * below * below;
            return 1.0 + 99.0 * std::pow(position, power);
        }
    }

    double DifficultyCalculator::calculate(const Chart& chart) {
        return calculate(chart.hits, chart.info.key_count, chart.timing_points, RANKED_HIT_ACC_DIFFICULTY);
    }

    double DifficultyCalculator::calculate_at_pitch(const Chart& chart, float pitch) {
        std::vector<Hit> hits;
        std::vector<ChartTimingPoint> timing_points;
        chart.pitched_timing(pitch, hits, timing_points);
        return calculate(hits, chart.unpitched_key_count(), timing_points, RANKED_HIT_ACC_DIFFICULTY);
    }

    double DifficultyCalculator::strictness_factor(uint8_t hit_accuracy) {
        double accuracy = std::clamp<double>(hit_accuracy, MIN_HIT_ACC_DIFFICULTY, MAX_HIT_ACC_DIFFICULTY);
        double widest = 2.0 * MAX_HIT_ACC_DIFFICULTY;
        return (widest - STRICTNESS_REFERENCE) / (widest - accuracy);
    }

    double DifficultyCalculator::calculate(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy) {
        if (hits.empty() || key_count == 0) {
            return 0.0;
        }

        std::vector<Hit> sorted_hits = hits;
        std::vector<ChartTimingPoint> sorted_points = timing_points;

        std::stable_sort(sorted_hits.begin(), sorted_hits.end(), [](const Hit& a, const Hit& b) {
            return a.time < b.time;
        });

        std::stable_sort(sorted_points.begin(), sorted_points.end(), [](const ChartTimingPoint& a, const ChartTimingPoint& b) {
            return a.time < b.time;
        });

        return displayed(skill_rating(sorted_hits, key_count, sorted_points)) * strictness_factor(hit_accuracy);
    }
}
