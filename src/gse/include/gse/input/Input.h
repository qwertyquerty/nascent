#pragma once

#include "gse/input/KeyEvents.h"
#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nascent {
    using ActionId = uint32_t;

    namespace actions {
        inline constexpr ActionId BACK = 0;
        inline constexpr ActionId CONFIRM = 1;
        inline constexpr ActionId UP = 2;
        inline constexpr ActionId DOWN = 3;
        inline constexpr ActionId LEFT = 4;
        inline constexpr ActionId RIGHT = 5;
        inline constexpr ActionId NEXT_FIELD = 6;
        inline constexpr ActionId FIRST_CUSTOM = 32;
    }

    class Input {
        public:
        using KeyState = std::function<olc::HWButton(olc::Key)>;
        static constexpr size_t MOUSE_BUTTONS = 3;

        protected:
        olc::PixelGameEngine* window = nullptr;
        KeyState key_state;
        KeyEventQueue queue;
        bool hook_active = false;
        bool keys_from_queue = false;
        InputMode input_mode = InputMode::POLLING;
        std::array<olc::HWButton, KEY_COUNT> queued_keys = {};
        std::vector<KeyEvent> frame_events;
        std::string frame_text;
        std::vector<std::string> frame_dropped_files;
        std::vector<olc::Key> frame_pressed_keys;
        std::unordered_map<ActionId, std::vector<olc::Key>> bindings;
        std::unordered_set<ActionId> consumed;
        std::unordered_set<ActionId> injected;
        std::unordered_set<ActionId> blocked;
        std::array<bool, MOUSE_BUTTONS> consumed_clicks = {};
        bool back_clicked = false;
        bool wheel_consumed = false;
        int suppressed = 0;

        void poll_key_events();
        void update_queued_keys();

        public:
        explicit Input(olc::PixelGameEngine* window);
        explicit Input(KeyState key_state);
        ~Input();
        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;

        void start();
        void begin_frame();

        void bind(ActionId action, std::initializer_list<olc::Key> keys);
        bool pressed(ActionId action) const;
        bool held(ActionId action) const;
        bool take(ActionId action);
        void consume(ActionId action);
        void inject(ActionId action);
        void block(ActionId action);

        bool focused() const;
        olc::HWButton key(olc::Key key) const;
        bool key_pressed(olc::Key key) const;
        bool key_held(olc::Key key) const;

        void suppress();
        void restore();
        bool is_suppressed() const;

        const std::vector<KeyEvent>& events() const;
        const std::string& text() const;
        const std::vector<std::string>& dropped_files() const;
        const std::vector<olc::Key>& pressed_keys() const;
        int32_t mouse_wheel() const;
        olc::vi2d mouse_position() const;
        olc::HWButton mouse(uint32_t button) const;
        bool clicked(uint32_t button = 0) const;
        bool take_click(uint32_t button = 0);
        int32_t take_wheel();
        olc::PixelGameEngine* target() const;
        KeyEventQueue& event_queue();
        InputMode mode() const;
        InputDelayStats delay() const;
        void reset_delay();
    };

    class SuppressedInput {
        Input& input;
        bool active;

        public:
        SuppressedInput(Input& input, bool active);
        ~SuppressedInput();
        SuppressedInput(const SuppressedInput&) = delete;
        SuppressedInput& operator=(const SuppressedInput&) = delete;
    };
}
