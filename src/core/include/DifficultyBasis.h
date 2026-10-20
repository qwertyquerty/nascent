#pragma once

#include "DifficultyEvents.h"

#include <array>
#include <bitset>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nascent {
    enum class BasisComponent : uint8_t {
        JACK,
        SUSTAINED_JACK,
        HAND_SPEED,
        HAND_CHORD,
        CHORDJACK,
        JUMPTRILL,
        DENSITY,
        HAND_ALTERNATION,
        RHYTHM,
        READING,
        LN_BODY,
        LN_PRESS,
        RELEASE_PRECISION,
        RELEASE_SPEED,
        NOTES,
        ROLL_SPEED,
        PATTERN_VARIETY,
        TRANSITION,
        JACK_SHORT,
        JACK_LONG,
        HAND_SPEED_SHORT,
        HAND_SPEED_LONG,
    };

    inline constexpr std::array<std::string_view, 22> BASIS_COMPONENT_NAMES = {
        "jack",
        "sustained_jack",
        "hand_speed",
        "hand_chord",
        "chordjack",
        "jumptrill",
        "density",
        "hand_alternation",
        "rhythm",
        "reading",
        "ln_body",
        "ln_press",
        "release_precision",
        "release_speed",
        "notes",
        "roll_speed",
        "pattern_variety",
        "transition",
        "jack_short",
        "jack_long",
        "hand_speed_short",
        "hand_speed_long",
    };

    enum class JackCurveKind : uint8_t {
        CAP,
        KNEE,
    };

    struct JackCurve {
        std::string_view name;
        JackCurveKind kind;
        double gap_ms;
    };

    inline constexpr std::array<JackCurve, 7> BASIS_JACK_CURVES = {{
        {"jack_cap_60ms", JackCurveKind::CAP, 60.0},
        {"jack_cap_80ms", JackCurveKind::CAP, 80.0},
        {"jack_cap_100ms", JackCurveKind::CAP, 100.0},
        {"jack_cap_130ms", JackCurveKind::CAP, 130.0},
        {"jack_knee_150ms", JackCurveKind::KNEE, 150.0},
        {"jack_knee_110ms", JackCurveKind::KNEE, 110.0},
        {"jack_knee_80ms", JackCurveKind::KNEE, 80.0},
    }};

    inline constexpr std::array<std::string_view, 3> BASIS_JACK_CURVE_VARIANTS = {"", "short_", "long_"};

    inline constexpr std::array<std::string_view, 8> BASIS_LOAD_NAMES = {"column_max", "column_max_squared", "hand_max", "column_imbalance", "hand_imbalance", "fatigue", "column_spread", "hand_spread"};

    inline constexpr std::array<BasisComponent, 7> BASIS_MIX_COMPONENTS = {
        BasisComponent::JACK,
        BasisComponent::HAND_SPEED,
        BasisComponent::ROLL_SPEED,
        BasisComponent::CHORDJACK,
        BasisComponent::JUMPTRILL,
        BasisComponent::DENSITY,
        BasisComponent::LN_BODY,
    };

    inline constexpr std::array<BasisComponent, 7> BASIS_INTERACTION_COMPONENTS = {
        BasisComponent::JACK,
        BasisComponent::HAND_SPEED,
        BasisComponent::DENSITY,
        BasisComponent::CHORDJACK,
        BasisComponent::JUMPTRILL,
        BasisComponent::LN_BODY,
        BasisComponent::RELEASE_PRECISION,
    };

    inline constexpr size_t BASIS_COMPONENT_COUNT = BASIS_COMPONENT_NAMES.size();
    inline constexpr std::array<int, 3> BASIS_POWERS = {1, 2, 3};
    inline constexpr std::array<double, 9> BASIS_TIMESCALES = {0.1, 0.25, 0.5, 0.75, 1.5, 2.5, 8.0, 25.0, 75.0};
    inline constexpr std::array<size_t, 2> BASIS_INTERACTION_TIMESCALES = {1, 5};
    inline constexpr size_t BASIS_MIX_SHORT_TIMESCALE = 2;
    inline constexpr size_t BASIS_MIX_LONG_TIMESCALE = 6;
    inline constexpr size_t BASIS_FATIGUE_TIMESCALE = 4;
    inline constexpr size_t BASIS_INTERACTION_PAIRS = BASIS_INTERACTION_COMPONENTS.size() * (BASIS_INTERACTION_COMPONENTS.size() - 1) / 2;
    inline constexpr size_t BASIS_PATTERN_SIZE = BASIS_COMPONENT_COUNT * BASIS_POWERS.size() * BASIS_TIMESCALES.size();
    inline constexpr size_t BASIS_LOAD_SIZE = BASIS_LOAD_NAMES.size() * BASIS_TIMESCALES.size();
    inline constexpr size_t BASIS_INTERACTION_SIZE = BASIS_INTERACTION_PAIRS * BASIS_INTERACTION_TIMESCALES.size();
    inline constexpr size_t BASIS_JACK_CURVE_COUNT = BASIS_JACK_CURVE_VARIANTS.size() * BASIS_JACK_CURVES.size();
    inline constexpr size_t BASIS_JACK_CURVE_SIZE = BASIS_JACK_CURVE_COUNT * BASIS_TIMESCALES.size();
    inline constexpr size_t BASIS_SIZE = BASIS_PATTERN_SIZE + BASIS_LOAD_SIZE + BASIS_INTERACTION_SIZE + BASIS_JACK_CURVE_SIZE;
    inline constexpr size_t BASIS_MAX_COLUMNS = 18;
    inline constexpr size_t BASIS_MAX_HANDS = 3;
    inline constexpr double BASIS_SPEED_REFERENCE_MS = 100.0;
    inline constexpr double BASIS_MIN_GAP_MS = 30.0;
    inline constexpr double BASIS_JACK_CAP_SHARPNESS = 10.0;
    inline constexpr double BASIS_JACK_RUN_SHORT = 5.0;
    inline constexpr double BASIS_JACK_RUN_LONG = 12.0;
    inline constexpr double BASIS_JACK_KNEE_WIDTH = 0.1;
    inline constexpr double BASIS_MAX_RHYTHM_RATIO = 2.0;

    using BasisComponents = std::array<double, BASIS_COMPONENT_COUNT>;
    using JackCurveValues = std::array<double, BASIS_JACK_CURVE_COUNT>;
    using JackCurveBase = std::array<double, BASIS_JACK_CURVES.size()>;
    using BasisVector = std::array<double, BASIS_SIZE>;

    size_t basis_index(BasisComponent component, size_t power, size_t timescale);
    std::string basis_name(size_t index);
    BasisComponents basis_components(const DifficultyEvent& event);
    JackCurveValues jack_curve_values(const DifficultyEvent& event);
    double basis_mix_shift(const BasisVector& banks);
    bool basis_same_group(const DifficultyEvent& a, const DifficultyEvent& b);

    using BasisMask = std::bitset<BASIS_SIZE>;

    BasisMask basis_mask_with_dependencies(BasisMask used);

    class BasisAccumulator {
        std::array<std::vector<std::pair<uint8_t, uint8_t>>, BASIS_TIMESCALES.size()> pattern_slots;
        std::array<std::vector<uint8_t>, BASIS_TIMESCALES.size()> curve_slots;
        std::vector<std::pair<uint8_t, uint8_t>> interaction_slots;
        std::unordered_map<double, JackCurveBase> curve_memo;
        BasisVector banks = {};
        std::array<std::array<double, BASIS_TIMESCALES.size()>, BASIS_MAX_COLUMNS> column_banks = {};
        std::array<std::array<double, BASIS_TIMESCALES.size()>, BASIS_MAX_HANDS> hand_banks = {};
        std::array<double, BASIS_TIMESCALES.size()> fatigue_banks = {};
        std::array<std::array<double, BASIS_TIMESCALES.size()>, BASIS_MAX_COLUMNS> group_prior_columns = {};
        std::array<std::array<double, BASIS_TIMESCALES.size()>, BASIS_MAX_HANDS> group_prior_hands = {};
        double group_mix_shift = 0;
        double last_time = 0;
        bool last_press = true;
        bool started = false;

        public:
        BasisAccumulator();
        explicit BasisAccumulator(const BasisMask& mask);
        const BasisVector& push(const DifficultyEvent& event);
    };
}
