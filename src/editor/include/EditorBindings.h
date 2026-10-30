#pragma once

#include "json.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nascent {
    class Input;

    enum class EditorAction : uint8_t {
        NEW_FROM_AUDIO,
        OPEN_CHART,
        SAVE,
        SAVE_AS,
        TEST_PLAY,
        SETTINGS,
        QUIT,
        UNDO,
        REDO,
        SELECT_ALL,
        COPY,
        CUT,
        PASTE,
        DELETE_SELECTION,
        CLEAR_SELECTION,
        MIRROR,
        TRANSFORM,
        INVERSE,
        NUDGE_LATER,
        NUDGE_EARLIER,
        NUDGE_LEFT,
        NUDGE_RIGHT,
        TOGGLE_SNAP,
        COPY_TIMESTAMP,
        GO_TO_TIMESTAMP,
        PLAY_PAUSE,
        STEP_FORWARD,
        STEP_BACK,
        BEAT_FORWARD,
        BEAT_BACK,
        MS_FORWARD,
        MS_BACK,
        FINER_SNAP,
        COARSER_SNAP,
        GO_TO_START,
        GO_TO_END,
        NEXT_DIFFICULTY,
        PREVIOUS_DIFFICULTY,
        SCROLL_FASTER,
        SCROLL_SLOWER,
        LANE_WIDER,
        LANE_NARROWER,
        TAP_TEMPO,
        LANE_1,
        LANE_2,
        LANE_3,
        LANE_4,
        LANE_5,
        LANE_6,
        LANE_7,
        LANE_8,
        LANE_9,
        LANE_10,
        COUNT,
    };

    inline constexpr size_t EDITOR_ACTION_COUNT = (size_t)EditorAction::COUNT;
    inline constexpr size_t CHORDS_PER_ACTION = 2;

    struct KeyChord {
        olc::Key key = olc::Key::NONE;
        bool ctrl = false;
        bool shift = false;
        bool alt = false;

        bool empty() const;
        bool operator==(const KeyChord& other) const = default;
    };

    struct ActionInfo {
        const char* id;
        const char* label;
        const char* group;
        std::array<KeyChord, CHORDS_PER_ACTION> defaults;
    };

    const ActionInfo& action_info(EditorAction action);
    std::string key_text(olc::Key key);
    std::string chord_text(const KeyChord& chord);
    std::string chord_name(const KeyChord& chord);
    std::optional<KeyChord> parse_chord(std::string_view text);

    class EditorBindings {
        std::array<std::array<KeyChord, CHORDS_PER_ACTION>, EDITOR_ACTION_COUNT> chords;

        public:
        EditorBindings();

        const KeyChord& get(EditorAction action, size_t slot) const;
        void set(EditorAction action, size_t slot, const KeyChord& chord);
        void reset();
        void reset(EditorAction action);
        bool is_default(EditorAction action) const;
        std::string label(EditorAction action) const;
        std::optional<EditorAction> owner(const KeyChord& chord, EditorAction except) const;
        bool pressed(EditorAction action, const Input& input, bool alt) const;

        nlohmann::json to_json() const;
        void load(const nlohmann::json& data);
        bool operator==(const EditorBindings& other) const = default;
    };
}
