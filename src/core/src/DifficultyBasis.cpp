#include "DifficultyBasis.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace nascent {
    namespace {
        double channel(const DifficultyEvent& event, EventChannel name) {
            return event[(size_t)name];
        }

        double speed(double gap) {
            if (gap >= NO_PREVIOUS_GAP - 0.5) {
                return 0.0;
            }

            return BASIS_SPEED_REFERENCE_MS / std::max(gap, BASIS_MIN_GAP_MS);
        }

        double raised(double value, int power) {
            double result = value;

            for (int step = 1; step < power; step++) {
                result *= value;
            }

            return result;
        }

        size_t load_index(size_t load, size_t timescale) {
            return BASIS_PATTERN_SIZE + load * BASIS_TIMESCALES.size() + timescale;
        }

        size_t interaction_index(size_t pair, size_t timescale) {
            return BASIS_PATTERN_SIZE + BASIS_LOAD_SIZE + pair * BASIS_INTERACTION_TIMESCALES.size() + timescale;
        }

        size_t jack_curve_index(size_t curve, size_t timescale) {
            return BASIS_PATTERN_SIZE + BASIS_LOAD_SIZE + BASIS_INTERACTION_SIZE + curve * BASIS_TIMESCALES.size() + timescale;
        }

        double softplus(double value) {
            return value > 30.0 ? value : std::log1p(std::exp(value));
        }

        double long_run_share(const DifficultyEvent& event) {
            double run = std::max(1.0, channel(event, EventChannel::JACK_RUN));
            return std::clamp(std::log(run / BASIS_JACK_RUN_SHORT) / std::log(BASIS_JACK_RUN_LONG / BASIS_JACK_RUN_SHORT), 0.0, 1.0);
        }

        constexpr std::array<std::pair<uint8_t, uint8_t>, BASIS_INTERACTION_PAIRS> interaction_pairs() {
            std::array<std::pair<uint8_t, uint8_t>, BASIS_INTERACTION_PAIRS> pairs = {};
            size_t pair = 0;

            for (size_t first = 0; first < BASIS_INTERACTION_COMPONENTS.size(); first++) {
                for (size_t second = first + 1; second < BASIS_INTERACTION_COMPONENTS.size(); second++) {
                    pairs[pair++] = {(uint8_t)first, (uint8_t)second};
                }
            }

            return pairs;
        }

        constexpr std::array<std::pair<uint8_t, uint8_t>, BASIS_INTERACTION_PAIRS> INTERACTION_PAIRS = interaction_pairs();

        struct CurveKnots {
            std::array<double, BASIS_JACK_CURVES.size()> speed;
            std::array<double, BASIS_JACK_CURVES.size()> log_speed;
        };

        const CurveKnots& curve_knots() {
            static const CurveKnots knots = [] {
                CurveKnots result = {};

                for (size_t curve = 0; curve < BASIS_JACK_CURVES.size(); curve++) {
                    result.speed[curve] = speed(BASIS_JACK_CURVES[curve].gap_ms);
                    result.log_speed[curve] = std::log(result.speed[curve]);
                }

                return result;
            }();

            return knots;
        }

        constexpr std::array<double, BASIS_TIMESCALES.size()> inverse_timescales() {
            std::array<double, BASIS_TIMESCALES.size()> inverse = {};

            for (size_t timescale = 0; timescale < BASIS_TIMESCALES.size(); timescale++) {
                inverse[timescale] = 1.0 / BASIS_TIMESCALES[timescale];
            }

            return inverse;
        }

        constexpr std::array<double, BASIS_TIMESCALES.size()> INVERSE_TIMESCALES = inverse_timescales();
        constexpr double NEGLIGIBLE_EXPONENT = -36.0;

        double event_jack_speed(const DifficultyEvent& event) {
            return channel(event, EventChannel::RELEASE) > 0.5 ? 0.0 : speed(channel(event, EventChannel::COLUMN_GAP));
        }

        JackCurveBase jack_curve_base(double jack) {
            JackCurveBase values = {};
            const CurveKnots& knots = curve_knots();
            double log_jack = std::log(jack);

            for (size_t curve = 0; curve < BASIS_JACK_CURVES.size(); curve++) {
                double knot = knots.speed[curve];

                if (BASIS_JACK_CURVES[curve].kind == JackCurveKind::CAP) {
                    double excess = BASIS_JACK_CAP_SHARPNESS * (log_jack - knots.log_speed[curve]);
                    values[curve] = excess < NEGLIGIBLE_EXPONENT ? jack : jack * std::exp(-softplus(excess) / BASIS_JACK_CAP_SHARPNESS);
                }
                else {
                    double width = BASIS_JACK_KNEE_WIDTH * knot;
                    double excess = (jack - knot) / width;
                    values[curve] = width * (excess < NEGLIGIBLE_EXPONENT ? std::exp(excess) : softplus(excess));
                }
            }

            return values;
        }

        double solo_share(const DifficultyEvent& event) {
            return channel(event, EventChannel::HAND_CHORD_SIZE) > 1.0 ? 0.0 : 1.0;
        }

        JackCurveValues split_jack_curves(const JackCurveBase& base, double long_run, double solo) {
            JackCurveValues values = {};
            size_t count = BASIS_JACK_CURVES.size();

            for (size_t curve = 0; curve < count; curve++) {
                values[curve] = base[curve];
                values[count + curve] = solo * (1.0 - long_run) * base[curve];
                values[2 * count + curve] = solo * long_run * base[curve];
                values[3 * count + curve] = (1.0 - solo) * (1.0 - long_run) * base[curve];
                values[4 * count + curve] = (1.0 - solo) * long_run * base[curve];
            }

            return values;
        }
    }

    BasisMask basis_mask_with_dependencies(BasisMask used) {
        for (BasisComponent component : BASIS_MIX_COMPONENTS) {
            used.set(basis_index(component, 0, BASIS_MIX_SHORT_TIMESCALE));
            used.set(basis_index(component, 0, BASIS_MIX_LONG_TIMESCALE));
        }

        for (size_t pair = 0; pair < BASIS_INTERACTION_PAIRS; pair++) {
            for (size_t slot = 0; slot < BASIS_INTERACTION_TIMESCALES.size(); slot++) {
                if (used.test(interaction_index(pair, slot))) {
                    used.set(basis_index(BASIS_INTERACTION_COMPONENTS[INTERACTION_PAIRS[pair].first], 0, BASIS_INTERACTION_TIMESCALES[slot]));
                    used.set(basis_index(BASIS_INTERACTION_COMPONENTS[INTERACTION_PAIRS[pair].second], 0, BASIS_INTERACTION_TIMESCALES[slot]));
                }
            }
        }

        return used;
    }

    BasisAccumulator::BasisAccumulator() : BasisAccumulator(BasisMask().set()) {
    }

    BasisAccumulator::BasisAccumulator(const BasisMask& mask) {
        BasisMask needed = basis_mask_with_dependencies(mask);

        for (size_t timescale = 0; timescale < BASIS_TIMESCALES.size(); timescale++) {
            for (size_t component = 0; component < BASIS_COMPONENT_COUNT; component++) {
                for (size_t power = 0; power < BASIS_POWERS.size(); power++) {
                    if (needed.test(basis_index((BasisComponent)component, power, timescale))) {
                        pattern_slots[timescale].push_back({(uint8_t)component, (uint8_t)power});
                    }
                }
            }

            for (size_t curve = 0; curve < BASIS_JACK_CURVE_COUNT; curve++) {
                if (needed.test(jack_curve_index(curve, timescale))) {
                    curve_slots[timescale].push_back((uint8_t)curve);
                }
            }
        }

        for (size_t pair = 0; pair < BASIS_INTERACTION_PAIRS; pair++) {
            for (size_t slot = 0; slot < BASIS_INTERACTION_TIMESCALES.size(); slot++) {
                if (needed.test(interaction_index(pair, slot))) {
                    interaction_slots.push_back({(uint8_t)pair, (uint8_t)slot});
                }
            }
        }
    }

    size_t basis_index(BasisComponent component, size_t power, size_t timescale) {
        return ((size_t)component * BASIS_POWERS.size() + power) * BASIS_TIMESCALES.size() + timescale;
    }

    std::string basis_name(size_t index) {
        if (index >= BASIS_PATTERN_SIZE + BASIS_LOAD_SIZE + BASIS_INTERACTION_SIZE) {
            size_t offset = index - BASIS_PATTERN_SIZE - BASIS_LOAD_SIZE - BASIS_INTERACTION_SIZE;
            size_t curve = offset / BASIS_TIMESCALES.size();
            return std::format("{}{}@{}s", BASIS_JACK_CURVE_VARIANTS[curve / BASIS_JACK_CURVES.size()], BASIS_JACK_CURVES[curve % BASIS_JACK_CURVES.size()].name, BASIS_TIMESCALES[offset % BASIS_TIMESCALES.size()]);
        }

        if (index >= BASIS_PATTERN_SIZE + BASIS_LOAD_SIZE) {
            size_t offset = index - BASIS_PATTERN_SIZE - BASIS_LOAD_SIZE;
            size_t pair = offset / BASIS_INTERACTION_TIMESCALES.size();
            size_t timescale = BASIS_INTERACTION_TIMESCALES[offset % BASIS_INTERACTION_TIMESCALES.size()];
            size_t first = 0;

            while (pair >= BASIS_INTERACTION_COMPONENTS.size() - 1 - first) {
                pair -= BASIS_INTERACTION_COMPONENTS.size() - 1 - first;
                first++;
            }

            size_t second = first + 1 + pair;
            return std::format("{}*{}@{}s", BASIS_COMPONENT_NAMES[(size_t)BASIS_INTERACTION_COMPONENTS[first]], BASIS_COMPONENT_NAMES[(size_t)BASIS_INTERACTION_COMPONENTS[second]], BASIS_TIMESCALES[timescale]);
        }

        if (index >= BASIS_PATTERN_SIZE) {
            size_t offset = index - BASIS_PATTERN_SIZE;
            return std::format("{}@{}s", BASIS_LOAD_NAMES[offset / BASIS_TIMESCALES.size()], BASIS_TIMESCALES[offset % BASIS_TIMESCALES.size()]);
        }

        size_t timescale = index % BASIS_TIMESCALES.size();
        size_t power = index / BASIS_TIMESCALES.size() % BASIS_POWERS.size();
        size_t component = index / BASIS_TIMESCALES.size() / BASIS_POWERS.size();
        return std::format("{}^{}@{}s", BASIS_COMPONENT_NAMES[component], BASIS_POWERS[power], BASIS_TIMESCALES[timescale]);
    }

    BasisComponents basis_components(const DifficultyEvent& event) {
        BasisComponents values = {};
        bool release = channel(event, EventChannel::RELEASE) > 0.5;
        values[(size_t)BasisComponent::NOTES] = 1.0;

        if (release) {
            values[(size_t)BasisComponent::RELEASE_PRECISION] = speed(channel(event, EventChannel::NEAREST_HAND_EVENT));
            values[(size_t)BasisComponent::RELEASE_SPEED] = speed(channel(event, EventChannel::HAND_GAP));
            return values;
        }

        double chord = std::max(1.0, channel(event, EventChannel::CHORD_SIZE));
        double hand_chord = std::max(1.0, channel(event, EventChannel::HAND_CHORD_SIZE));
        double overlap = channel(event, EventChannel::CHORD_OVERLAP);
        double onset = speed(channel(event, EventChannel::ONSET_GAP));
        double hand = speed(channel(event, EventChannel::HAND_GAP));
        double column_gap = channel(event, EventChannel::COLUMN_GAP);
        double onset_gap = channel(event, EventChannel::ONSET_GAP);
        double previous_onset_gap = channel(event, EventChannel::PREVIOUS_ONSET_GAP);
        double roll = std::clamp(channel(event, EventChannel::ROLL_EVENNESS), 0.0, 1.0);

        values[(size_t)BasisComponent::JACK] = speed(column_gap);
        values[(size_t)BasisComponent::SUSTAINED_JACK] = speed(std::max(column_gap, channel(event, EventChannel::PREVIOUS_COLUMN_GAP)));
        double long_run = long_run_share(event);
        values[(size_t)BasisComponent::JACK_SHORT] = (1.0 - long_run) * speed(column_gap);
        values[(size_t)BasisComponent::JACK_LONG] = long_run * speed(column_gap);
        double trill = channel(event, EventChannel::TRILL);
        values[(size_t)BasisComponent::HAND_SPEED] = (1.0 - roll) * (1.0 - trill) * hand / hand_chord;
        values[(size_t)BasisComponent::HAND_TRILL] = (1.0 - roll) * trill * hand / hand_chord;
        values[(size_t)BasisComponent::ANCHOR] = channel(event, EventChannel::ANCHOR) * hand / hand_chord;
        values[(size_t)BasisComponent::FLAM] = std::clamp(channel(event, EventChannel::FLAM_GAP) / CHORD_SEPARATE_MS, 0.0, 1.0) * onset / chord;
        values[(size_t)BasisComponent::BURST] = std::max(0.0, channel(event, EventChannel::BURST) - 1.0) * onset / chord;
        values[(size_t)BasisComponent::HAND_SPEED_SHORT] = (1.0 - long_run) * values[(size_t)BasisComponent::HAND_SPEED];
        values[(size_t)BasisComponent::HAND_SPEED_LONG] = long_run * values[(size_t)BasisComponent::HAND_SPEED];
        values[(size_t)BasisComponent::ROLL_SPEED] = roll * hand / hand_chord;
        values[(size_t)BasisComponent::PATTERN_VARIETY] = channel(event, EventChannel::PATTERN_VARIETY) * onset / chord;
        values[(size_t)BasisComponent::HAND_CHORD] = (hand_chord - 1.0) / hand_chord * hand;
        values[(size_t)BasisComponent::CHORDJACK] = chord > 1.0 ? overlap * onset : 0.0;
        values[(size_t)BasisComponent::JUMPTRILL] = chord > 1.0 ? (1.0 - overlap) * onset : 0.0;
        values[(size_t)BasisComponent::DENSITY] = onset / chord;
        values[(size_t)BasisComponent::HAND_ALTERNATION] = channel(event, EventChannel::HAND_SWITCH) * speed(channel(event, EventChannel::OTHER_HAND_GAP)) / hand_chord;

        if (onset_gap < NO_PREVIOUS_GAP - 0.5 && previous_onset_gap < NO_PREVIOUS_GAP - 0.5 && onset_gap > 0 && previous_onset_gap > 0) {
            values[(size_t)BasisComponent::RHYTHM] = std::min(std::abs(std::log2(onset_gap / previous_onset_gap)), BASIS_MAX_RHYTHM_RATIO) * onset / chord;
        }

        values[(size_t)BasisComponent::READING] = channel(event, EventChannel::SNAP) * onset / chord;
        values[(size_t)BasisComponent::LN_BODY] = channel(event, EventChannel::HELD_SAME_HAND) * hand / hand_chord;
        values[(size_t)BasisComponent::LN_PRESS] = channel(event, EventChannel::HOLD) * hand / hand_chord;
        return values;
    }

    RowAttributes basis_row_attributes(const DifficultyEvent& event) {
        RowAttributes values = {};

        if (channel(event, EventChannel::RELEASE) > 0.5) {
            return values;
        }

        double chord = channel(event, EventChannel::CHORD_SIZE);
        values[0] = chord >= 1.5 && chord < 2.5 ? 1.0 : 0.0;
        values[1] = chord >= 2.5 && chord < 3.5 ? 1.0 : 0.0;
        values[2] = chord >= 3.5 ? 1.0 : 0.0;
        values[3] = channel(event, EventChannel::HAND_CHORD_SIZE) >= 1.5 ? 1.0 : 0.0;
        values[4] = channel(event, EventChannel::HOLD) > 0.5 ? 1.0 : 0.0;
        values[5] = std::clamp(channel(event, EventChannel::HELD_SAME_HAND), 0.0, 1.0);
        values[6] = std::clamp(channel(event, EventChannel::TRILL), 0.0, 1.0);
        values[7] = std::clamp(channel(event, EventChannel::ANCHOR), 0.0, 1.0);
        values[8] = channel(event, EventChannel::FLAM_GAP) > 0.0 ? std::clamp(channel(event, EventChannel::FLAM_GAP) / CHORD_SEPARATE_MS, 0.0, 1.0) : 0.0;
        values[9] = std::clamp((BASIS_ROW_JACK_SLOW_MS - channel(event, EventChannel::COLUMN_GAP)) / (BASIS_ROW_JACK_SLOW_MS - BASIS_ROW_JACK_FAST_MS), 0.0, 1.0);
        values[10] = long_run_share(event);
        values[12] = std::clamp((BASIS_ROW_VIBRO_SLOW_MS - channel(event, EventChannel::COLUMN_GAP)) / (BASIS_ROW_VIBRO_SLOW_MS - BASIS_ROW_VIBRO_FAST_MS), 0.0, 1.0);
        values[11] = values[12] * values[10];
        double chorded = chord >= 1.5 ? std::clamp(1.0 - channel(event, EventChannel::CHORD_SPREAD) / BASIS_ROW_CHORD_SPREAD_MS, 0.0, 1.0) : 0.0;
        values[13] = values[11] * chorded;
        values[14] = std::clamp(1.0 - std::abs(channel(event, EventChannel::COLUMN_GAP) - BASIS_ROW_JACK_BAND_MS) / BASIS_ROW_JACK_BAND_WIDTH_MS, 0.0, 1.0);
        values[15] = values[9] * values[10] * chorded;
        return values;
    }

    JackCurveValues jack_curve_values(const DifficultyEvent& event) {
        double jack = event_jack_speed(event);
        return jack > 0 ? split_jack_curves(jack_curve_base(jack), long_run_share(event), solo_share(event)) : JackCurveValues{};
    }

    double basis_mix_shift(const BasisVector& banks) {
        double short_total = 0;
        double long_total = 0;

        for (BasisComponent component : BASIS_MIX_COMPONENTS) {
            short_total += banks[basis_index(component, 0, BASIS_MIX_SHORT_TIMESCALE)];
            long_total += banks[basis_index(component, 0, BASIS_MIX_LONG_TIMESCALE)];
        }

        if (short_total <= 0 || long_total <= 0) {
            return 0.0;
        }

        double distance = 0;

        for (BasisComponent component : BASIS_MIX_COMPONENTS) {
            distance += std::abs(banks[basis_index(component, 0, BASIS_MIX_SHORT_TIMESCALE)] / short_total - banks[basis_index(component, 0, BASIS_MIX_LONG_TIMESCALE)] / long_total);
        }

        return 0.5 * distance;
    }

    bool basis_same_group(const DifficultyEvent& a, const DifficultyEvent& b) {
        return channel(a, EventChannel::TIME) == channel(b, EventChannel::TIME) && (channel(a, EventChannel::RELEASE) > 0.5) == (channel(b, EventChannel::RELEASE) > 0.5);
    }

    const BasisVector& BasisAccumulator::push(const DifficultyEvent& event) {
        double time = channel(event, EventChannel::TIME) / 1000.0;
        bool press = channel(event, EventChannel::RELEASE) < 0.5;
        bool fresh = !started || time != last_time || press != last_press;
        double elapsed = started ? std::max(0.0, time - last_time) : 0.0;
        started = true;
        last_time = time;
        last_press = press;

        BasisComponents components = basis_components(event);
        JackCurveValues curves = {};
        double jack = event_jack_speed(event);

        if (jack > 0) {
            auto [found, added] = curve_memo.try_emplace(jack);

            if (added) {
                found->second = jack_curve_base(jack);
            }

            curves = split_jack_curves(found->second, long_run_share(event), solo_share(event));
        }

        if (fresh) {
            group_mix_shift = basis_mix_shift(banks);
        }

        if (press) {
            components[(size_t)BasisComponent::TRANSITION] = group_mix_shift * speed(channel(event, EventChannel::ONSET_GAP)) / std::max(1.0, channel(event, EventChannel::CHORD_SIZE));
        }

        size_t column = std::min((size_t)channel(event, EventChannel::COLUMN), BASIS_MAX_COLUMNS - 1);
        size_t hand = std::min((size_t)channel(event, EventChannel::HAND), BASIS_MAX_HANDS - 1);
        size_t key_count = std::clamp<size_t>((size_t)channel(event, EventChannel::KEY_COUNT), 1, BASIS_MAX_COLUMNS);
        size_t columns = std::max(key_count, column + 1);
        std::array<std::array<double, BASIS_POWERS.size()>, BASIS_COMPONENT_COUNT> powered;

        for (size_t component = 0; component < BASIS_COMPONENT_COUNT; component++) {
            for (size_t power = 0; power < BASIS_POWERS.size(); power++) {
                powered[component][power] = raised(components[component], BASIS_POWERS[power]);
            }
        }

        std::array<double, BASIS_TIMESCALES.size()> decays;

        for (size_t timescale = 0; timescale < BASIS_TIMESCALES.size(); timescale++) {
            double inverse = INVERSE_TIMESCALES[timescale];
            double decay = elapsed > 0 ? std::exp(-elapsed * inverse) : 1.0;
            decays[timescale] = decay;
            double column_max = 0;
            double column_total = 0;
            double hand_max = 0;
            double prior_column_max = 0;
            double prior_column_total = 0;
            std::array<double, BASIS_MAX_HANDS> prior_hands = {};

            for (size_t index = 0; index < columns; index++) {
                double prior = column_banks[index][timescale] * decay;

                if (fresh) {
                    group_prior_columns[index][timescale] = prior;
                }

                column_banks[index][timescale] = prior + (press && index == column ? inverse : 0.0);

                if (index < key_count) {
                    column_max = std::max(column_max, column_banks[index][timescale]);
                    column_total += column_banks[index][timescale];
                    prior_column_max = std::max(prior_column_max, group_prior_columns[index][timescale]);
                    prior_column_total += group_prior_columns[index][timescale];
                }
            }

            for (size_t index = 0; index < BASIS_MAX_HANDS; index++) {
                double prior = hand_banks[index][timescale] * decay;

                if (fresh) {
                    group_prior_hands[index][timescale] = prior;
                }

                prior_hands[index] = group_prior_hands[index][timescale];
                hand_banks[index][timescale] = prior + (press && index == hand ? inverse : 0.0);
                hand_max = std::max(hand_max, hand_banks[index][timescale]);
            }

            double column_mean = column_total / (double)key_count;
            double hand_mean = 0.5 * (hand_banks[0][timescale] + hand_banks[1][timescale]);
            banks[load_index(0, timescale)] = column_max;
            banks[load_index(1, timescale)] = column_max * column_max;
            banks[load_index(2, timescale)] = hand_max;
            banks[load_index(3, timescale)] = column_mean > 0 ? column_max / column_mean : 0.0;
            banks[load_index(4, timescale)] = hand_mean > 0 ? std::max(hand_banks[0][timescale], hand_banks[1][timescale]) / hand_mean : 0.0;
            banks[load_index(6, timescale)] = prior_column_max - prior_column_total / (double)key_count;
            banks[load_index(7, timescale)] = std::max(prior_hands[0], prior_hands[1]) - 0.5 * (prior_hands[0] + prior_hands[1]);

            for (const auto& [component, power] : pattern_slots[timescale]) {
                size_t index = basis_index((BasisComponent)component, power, timescale);
                banks[index] = banks[index] * decay + powered[component][power] * inverse;
            }

            for (uint8_t curve : curve_slots[timescale]) {
                size_t index = jack_curve_index(curve, timescale);
                banks[index] = banks[index] * decay + curves[curve] * inverse;
            }
        }

        double intensity = 0;

        for (size_t index = 0; index < key_count; index++) {
            intensity += column_banks[index][BASIS_FATIGUE_TIMESCALE];
        }

        for (size_t timescale = 0; timescale < BASIS_TIMESCALES.size(); timescale++) {
            fatigue_banks[timescale] = std::max(fatigue_banks[timescale] * decays[timescale], intensity);
            banks[load_index(5, timescale)] = fatigue_banks[timescale];
        }

        for (const auto& [pair, slot] : interaction_slots) {
            size_t timescale = BASIS_INTERACTION_TIMESCALES[slot];
            double a = banks[basis_index(BASIS_INTERACTION_COMPONENTS[INTERACTION_PAIRS[pair].first], 0, timescale)];
            double b = banks[basis_index(BASIS_INTERACTION_COMPONENTS[INTERACTION_PAIRS[pair].second], 0, timescale)];
            banks[interaction_index(pair, slot)] = std::sqrt(a * b);
        }

        return banks;
    }
}
