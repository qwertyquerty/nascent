#include "EditorBindings.h"

#include "gse/input/Input.h"
#include "gse/input/KeyNames.h"

#include <format>
#include <vector>

namespace nascent {
    namespace {
        using K = olc::Key;

        constexpr KeyChord key(K key) {
            return {key, false, false, false};
        }

        constexpr KeyChord ctrl(K key) {
            return {key, true, false, false};
        }

        constexpr KeyChord ctrl_shift(K key) {
            return {key, true, true, false};
        }

        constexpr KeyChord shift(K key) {
            return {key, false, true, false};
        }

        constexpr KeyChord alt(K key) {
            return {key, false, false, true};
        }

        constexpr KeyChord none() {
            return {};
        }

        constexpr std::array<ActionInfo, EDITOR_ACTION_COUNT> ACTIONS = {{
            {"new_from_audio", "New chart from audio", "File", {ctrl(K::N), none()}},
            {"open_chart", "Open chart", "File", {ctrl(K::O), none()}},
            {"save", "Save", "File", {ctrl(K::S), none()}},
            {"save_as", "Save as", "File", {ctrl_shift(K::S), none()}},
            {"test_play", "Test play from the playhead", "File", {key(K::F5), none()}},
            {"settings", "Settings", "File", {ctrl(K::COMMA), none()}},
            {"quit", "Quit", "File", {ctrl(K::Q), none()}},
            {"undo", "Undo", "Edit", {ctrl(K::Z), none()}},
            {"redo", "Redo", "Edit", {ctrl(K::Y), ctrl_shift(K::Z)}},
            {"select_all", "Select all", "Edit", {ctrl(K::A), none()}},
            {"copy", "Copy", "Edit", {ctrl(K::C), none()}},
            {"cut", "Cut", "Edit", {ctrl(K::X), none()}},
            {"paste", "Paste at the playhead", "Edit", {ctrl(K::V), none()}},
            {"delete_selection", "Delete the selected notes", "Edit", {key(K::DEL), key(K::BACK)}},
            {"clear_selection", "Clear the selection", "Edit", {key(K::ESCAPE), none()}},
            {"mirror", "Mirror the selection", "Edit", {key(K::M), none()}},
            {"transform", "Transform the selection", "Edit", {ctrl(K::T), none()}},
            {"inverse", "Inverse the selection", "Edit", {ctrl(K::I), none()}},
            {"nudge_later", "Move the selection one snap later", "Edit", {ctrl(K::UP), none()}},
            {"nudge_earlier", "Move the selection one snap earlier", "Edit", {ctrl(K::DOWN), none()}},
            {"nudge_left", "Move the selection one lane left", "Edit", {ctrl(K::LEFT), none()}},
            {"nudge_right", "Move the selection one lane right", "Edit", {ctrl(K::RIGHT), none()}},
            {"toggle_snap", "Snap notes on or off", "Edit", {key(K::N), none()}},
            {"copy_timestamp", "Copy a timestamp of the selection", "Edit", {ctrl_shift(K::C), none()}},
            {"go_to_timestamp", "Go to a copied timestamp", "Edit", {ctrl(K::G), none()}},
            {"play_pause", "Play or pause", "Move", {key(K::SPACE), none()}},
            {"step_forward", "Forward one snap", "Move", {key(K::RIGHT), none()}},
            {"step_back", "Back one snap", "Move", {key(K::LEFT), none()}},
            {"beat_forward", "Forward one beat", "Move", {key(K::UP), none()}},
            {"beat_back", "Back one beat", "Move", {key(K::DOWN), none()}},
            {"ms_forward", "Forward one millisecond", "Move", {alt(K::UP), alt(K::RIGHT)}},
            {"ms_back", "Back one millisecond", "Move", {alt(K::DOWN), alt(K::LEFT)}},
            {"finer_snap", "Finer snap", "Move", {shift(K::UP), shift(K::RIGHT)}},
            {"coarser_snap", "Coarser snap", "Move", {shift(K::DOWN), shift(K::LEFT)}},
            {"go_to_start", "Go to the start", "Move", {key(K::HOME), none()}},
            {"go_to_end", "Go to the last note", "Move", {key(K::END), none()}},
            {"next_difficulty", "Next difficulty tab", "Move", {ctrl(K::TAB), ctrl(K::PGDN)}},
            {"previous_difficulty", "Previous difficulty tab", "Move", {ctrl_shift(K::TAB), ctrl(K::PGUP)}},
            {"scroll_faster", "Faster scroll speed", "View", {ctrl(K::EQUALS), ctrl(K::NP_ADD)}},
            {"scroll_slower", "Slower scroll speed", "View", {ctrl(K::MINUS), ctrl(K::NP_SUB)}},
            {"lane_wider", "Wider lanes", "View", {ctrl_shift(K::UP), ctrl_shift(K::RIGHT)}},
            {"lane_narrower", "Narrower lanes", "View", {ctrl_shift(K::DOWN), ctrl_shift(K::LEFT)}},
            {"tap_tempo", "Tap tempo", "View", {key(K::T), none()}},
            {"lane_1", "Toggle a note in lane 1", "Lanes", {key(K::K1), none()}},
            {"lane_2", "Toggle a note in lane 2", "Lanes", {key(K::K2), none()}},
            {"lane_3", "Toggle a note in lane 3", "Lanes", {key(K::K3), none()}},
            {"lane_4", "Toggle a note in lane 4", "Lanes", {key(K::K4), none()}},
            {"lane_5", "Toggle a note in lane 5", "Lanes", {key(K::K5), none()}},
            {"lane_6", "Toggle a note in lane 6", "Lanes", {key(K::K6), none()}},
            {"lane_7", "Toggle a note in lane 7", "Lanes", {key(K::K7), none()}},
            {"lane_8", "Toggle a note in lane 8", "Lanes", {key(K::K8), none()}},
            {"lane_9", "Toggle a note in lane 9", "Lanes", {key(K::K9), none()}},
            {"lane_10", "Toggle a note in lane 10", "Lanes", {key(K::K0), none()}},
        }};

        std::vector<std::string_view> split(std::string_view text, char separator) {
            std::vector<std::string_view> parts;

            while (true) {
                size_t found = text.find(separator);
                parts.push_back(text.substr(0, found));

                if (found == std::string_view::npos) {
                    return parts;
                }

                text = text.substr(found + 1);
            }
        }
    }

    bool KeyChord::empty() const {
        return key == olc::Key::NONE;
    }

    const ActionInfo& action_info(EditorAction action) {
        return ACTIONS[(size_t)action];
    }

    std::string key_text(olc::Key key) {
        if (key >= olc::Key::K0 && key <= olc::Key::K9) {
            return std::string(1, (char)('0' + ((int)key - (int)olc::Key::K0)));
        }

        if (key >= olc::Key::NP0 && key <= olc::Key::NP9) {
            return std::format("Num {}", (int)key - (int)olc::Key::NP0);
        }

        switch (key) {
            case olc::Key::UP:
                return "Up";
            case olc::Key::DOWN:
                return "Down";
            case olc::Key::LEFT:
                return "Left";
            case olc::Key::RIGHT:
                return "Right";
            case olc::Key::SPACE:
                return "Space";
            case olc::Key::TAB:
                return "Tab";
            case olc::Key::INS:
                return "Insert";
            case olc::Key::DEL:
                return "Delete";
            case olc::Key::HOME:
                return "Home";
            case olc::Key::END:
                return "End";
            case olc::Key::PGUP:
                return "Page Up";
            case olc::Key::PGDN:
                return "Page Down";
            case olc::Key::BACK:
                return "Backspace";
            case olc::Key::ESCAPE:
                return "Esc";
            case olc::Key::RETURN:
            case olc::Key::ENTER:
                return "Enter";
            case olc::Key::PAUSE:
                return "Pause";
            case olc::Key::SCROLL:
                return "Scroll Lock";
            case olc::Key::NP_MUL:
                return "Num *";
            case olc::Key::NP_DIV:
                return "Num /";
            case olc::Key::NP_ADD:
                return "Num +";
            case olc::Key::NP_SUB:
                return "Num -";
            case olc::Key::NP_DECIMAL:
                return "Num .";
            case olc::Key::PERIOD:
                return ".";
            case olc::Key::EQUALS:
                return "=";
            case olc::Key::COMMA:
                return ",";
            case olc::Key::MINUS:
                return "-";
            case olc::Key::OEM_1:
                return ";";
            case olc::Key::OEM_2:
                return "/";
            case olc::Key::OEM_3:
                return "`";
            case olc::Key::OEM_4:
                return "[";
            case olc::Key::OEM_5:
                return "\\";
            case olc::Key::OEM_6:
                return "]";
            case olc::Key::OEM_7:
                return "'";
            case olc::Key::CAPS_LOCK:
                return "Caps Lock";
            default:
                return std::string(key_name(key));
        }
    }

    std::string chord_text(const KeyChord& chord) {
        if (chord.empty()) {
            return std::string();
        }

        return std::format("{}{}{}{}", chord.ctrl ? "Ctrl+" : "", chord.shift ? "Shift+" : "", chord.alt ? "Alt+" : "", key_text(chord.key));
    }

    std::string chord_name(const KeyChord& chord) {
        if (chord.empty()) {
            return std::string();
        }

        return std::format("{}{}{}{}", chord.ctrl ? "CTRL+" : "", chord.shift ? "SHIFT+" : "", chord.alt ? "ALT+" : "", key_name(chord.key));
    }

    std::optional<KeyChord> parse_chord(std::string_view text) {
        if (text.empty()) {
            return KeyChord();
        }

        std::vector<std::string_view> parts = split(text, '+');
        KeyChord chord;

        for (size_t index = 0; index + 1 < parts.size(); index++) {
            if (parts[index] == "CTRL") {
                chord.ctrl = true;
            }
            else if (parts[index] == "SHIFT") {
                chord.shift = true;
            }
            else if (parts[index] == "ALT") {
                chord.alt = true;
            }
            else {
                return std::nullopt;
            }
        }

        std::optional<olc::Key> found = key_from_name(parts.back());

        if (!found || *found == olc::Key::CTRL || *found == olc::Key::SHIFT) {
            return std::nullopt;
        }

        chord.key = *found;
        return chord;
    }

    EditorBindings::EditorBindings() {
        reset();
    }

    const KeyChord& EditorBindings::get(EditorAction action, size_t slot) const {
        return chords[(size_t)action][slot];
    }

    void EditorBindings::set(EditorAction action, size_t slot, const KeyChord& chord) {
        chords[(size_t)action][slot] = chord;
    }

    void EditorBindings::reset() {
        for (size_t index = 0; index < EDITOR_ACTION_COUNT; index++) {
            chords[index] = ACTIONS[index].defaults;
        }
    }

    void EditorBindings::reset(EditorAction action) {
        chords[(size_t)action] = ACTIONS[(size_t)action].defaults;
    }

    bool EditorBindings::is_default(EditorAction action) const {
        return chords[(size_t)action] == ACTIONS[(size_t)action].defaults;
    }

    std::string EditorBindings::label(EditorAction action) const {
        std::string text;

        for (const KeyChord& chord : chords[(size_t)action]) {
            if (!chord.empty()) {
                text += (text.empty() ? "" : " / ") + chord_text(chord);
            }
        }

        return text;
    }

    std::optional<EditorAction> EditorBindings::owner(const KeyChord& chord, EditorAction except) const {
        if (chord.empty()) {
            return std::nullopt;
        }

        for (size_t index = 0; index < EDITOR_ACTION_COUNT; index++) {
            if ((EditorAction)index == except) {
                continue;
            }

            for (const KeyChord& bound : chords[index]) {
                if (bound == chord) {
                    return (EditorAction)index;
                }
            }
        }

        return std::nullopt;
    }

    bool EditorBindings::pressed(EditorAction action, const Input& input, bool alt) const {
        bool ctrl = input.key_held(olc::Key::CTRL);
        bool shift = input.key_held(olc::Key::SHIFT);

        for (const KeyChord& chord : chords[(size_t)action]) {
            if (!chord.empty() && chord.ctrl == ctrl && chord.shift == shift && chord.alt == alt && input.key_pressed(chord.key)) {
                return true;
            }
        }

        return false;
    }

    nlohmann::json EditorBindings::to_json() const {
        nlohmann::json data = nlohmann::json::object();

        for (size_t index = 0; index < EDITOR_ACTION_COUNT; index++) {
            if (is_default((EditorAction)index)) {
                continue;
            }

            nlohmann::json list = nlohmann::json::array();

            for (const KeyChord& chord : chords[index]) {
                list.push_back(chord_name(chord));
            }

            data[ACTIONS[index].id] = std::move(list);
        }

        return data;
    }

    void EditorBindings::load(const nlohmann::json& data) {
        reset();

        if (!data.is_object()) {
            return;
        }

        for (size_t index = 0; index < EDITOR_ACTION_COUNT; index++) {
            auto found = data.find(ACTIONS[index].id);

            if (found == data.end() || !found->is_array()) {
                continue;
            }

            for (size_t slot = 0; slot < CHORDS_PER_ACTION && slot < found->size(); slot++) {
                if (!(*found)[slot].is_string()) {
                    continue;
                }

                if (std::optional<KeyChord> chord = parse_chord((*found)[slot].get<std::string>())) {
                    chords[index][slot] = *chord;
                }
            }
        }
    }
}
