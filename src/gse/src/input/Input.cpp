#include "gse/input/Input.h"

#include "gse/Platform.h"
#include "gse/input/KeySource.h"
#include "gse/input/TextInput.h"
#include "gse/input/WindowHook.h"

#include <optional>
#include <utility>

namespace nascent {
    static const std::vector<KeyEvent> NO_EVENTS;
    static const std::string NO_TEXT;
    static const std::vector<std::string> NO_FILES;
    static const std::vector<olc::Key> NO_KEYS;

    Input::Input(olc::PixelGameEngine* window) : window(window) {
        key_state = [window](olc::Key key) {
            return window->GetKey(key);
        };
    }

    Input::Input(KeyState key_state) : key_state(std::move(key_state)) {}

    Input::~Input() {
        if (window == nullptr) {
            return;
        }

        stop_key_source();

        if (hook_active) {
            uninstall_window_hook();
        }
    }

    void Input::start() {
        if (window == nullptr) {
            return;
        }

        hook_active = install_window_hook(&queue, window->sAppName);
        input_mode = hook_active ? InputMode::WINDOW_MESSAGES : InputMode::POLLING;

        if (std::optional<InputMode> source = start_key_source(&queue, window->sAppName)) {
            input_mode = *source;
        }

        if (input_mode == InputMode::COCOA_EVENTS) {
            keys_from_queue = true;
            key_state = [this](olc::Key key) {
                size_t index = (size_t)key;
                return index < KEY_COUNT ? queued_keys[index] : olc::HWButton{};
            };
        }

        set_window_hook_key_events(input_mode == InputMode::WINDOW_MESSAGES);
    }

    void Input::poll_key_events() {
        double now = now_ms();

        for (size_t index = 1; index < KEY_COUNT; index++) {
            olc::Key key = (olc::Key)index;
            queue.push(key, key_state(key).bHeld, now);
        }
    }

    bool Input::focused() const {
        if (window == nullptr) {
            return true;
        }

        return application_focused().value_or(window->IsFocused());
    }

    void Input::update_queued_keys() {
        for (olc::HWButton& button : queued_keys) {
            button.bPressed = false;
            button.bReleased = false;
        }

        for (const KeyEvent& event : frame_events) {
            olc::HWButton& button = queued_keys[(size_t)event.key];
            button.bPressed = button.bPressed || (event.pressed && !button.bHeld);
            button.bReleased = button.bReleased || (!event.pressed && button.bHeld);
            button.bHeld = event.pressed;
        }
    }

    void Input::begin_frame() {
        consumed.clear();
        injected.clear();
        blocked.clear();
        consumed_clicks.fill(false);
        wheel_consumed = false;
        back_clicked = queue.drain_back_click();

        if (input_mode == InputMode::POLLING) {
            poll_key_events();
        }

        frame_events = queue.drain();
        frame_text = hook_active || keys_from_queue || input_mode == InputMode::X11_THREAD || window == nullptr ? queue.drain_text() : us_layout_text(window);
        frame_dropped_files = queue.drain_dropped_files();
        frame_pressed_keys.clear();

        if (keys_from_queue) {
            update_queued_keys();

            for (const KeyEvent& event : frame_events) {
                if (event.pressed) {
                    frame_pressed_keys.push_back(event.key);
                }
            }

            for (olc::Key key : queue.drain_repeats()) {
                frame_pressed_keys.push_back(key);
            }
        }
        else if (window != nullptr) {
            for (int32_t keycode : window->GetKeyPressCache()) {
                frame_pressed_keys.push_back(window->ConvertKeycode(keycode));
            }
        }
    }

    void Input::bind(ActionId action, std::initializer_list<olc::Key> keys) {
        bindings[action] = keys;
    }

    bool Input::pressed(ActionId action) const {
        if (suppressed > 0 || consumed.contains(action) || blocked.contains(action)) {
            return false;
        }

        if ((action == actions::BACK && back_clicked) || injected.contains(action)) {
            return true;
        }

        auto found = bindings.find(action);

        if (found == bindings.end()) {
            return false;
        }

        for (olc::Key bound : found->second) {
            if (key_state(bound).bPressed) {
                return true;
            }
        }

        return false;
    }

    bool Input::held(ActionId action) const {
        if (suppressed > 0 || blocked.contains(action)) {
            return false;
        }

        auto found = bindings.find(action);

        if (found == bindings.end()) {
            return false;
        }

        for (olc::Key bound : found->second) {
            if (key_state(bound).bHeld) {
                return true;
            }
        }

        return false;
    }

    bool Input::take(ActionId action) {
        if (!pressed(action)) {
            return false;
        }

        consumed.insert(action);
        return true;
    }

    void Input::consume(ActionId action) {
        consumed.insert(action);
    }

    void Input::block(ActionId action) {
        blocked.insert(action);
    }

    void Input::inject(ActionId action) {
        injected.insert(action);
        consumed.erase(action);
    }

    olc::HWButton Input::key(olc::Key key) const {
        return suppressed > 0 ? olc::HWButton{} : key_state(key);
    }

    bool Input::key_pressed(olc::Key key) const {
        return this->key(key).bPressed;
    }

    bool Input::key_held(olc::Key key) const {
        return this->key(key).bHeld;
    }

    void Input::suppress() {
        suppressed++;
    }

    void Input::restore() {
        if (suppressed > 0) {
            suppressed--;
        }
    }

    bool Input::is_suppressed() const {
        return suppressed > 0;
    }

    const std::vector<KeyEvent>& Input::events() const {
        return suppressed > 0 ? NO_EVENTS : frame_events;
    }

    const std::string& Input::text() const {
        return suppressed > 0 ? NO_TEXT : frame_text;
    }

    const std::vector<std::string>& Input::dropped_files() const {
        return suppressed > 0 ? NO_FILES : frame_dropped_files;
    }

    const std::vector<olc::Key>& Input::pressed_keys() const {
        return suppressed > 0 ? NO_KEYS : frame_pressed_keys;
    }

    int32_t Input::mouse_wheel() const {
        return suppressed > 0 || window == nullptr || wheel_consumed ? 0 : window->GetMouseWheel();
    }

    int32_t Input::take_wheel() {
        int32_t wheel = mouse_wheel();
        wheel_consumed = true;
        return wheel;
    }

    bool Input::clicked(uint32_t button) const {
        return button < MOUSE_BUTTONS && !consumed_clicks[button] && mouse(button).bPressed;
    }

    bool Input::take_click(uint32_t button) {
        if (!clicked(button)) {
            return false;
        }

        consumed_clicks[button] = true;
        return true;
    }

    olc::vi2d Input::mouse_position() const {
        return window == nullptr ? olc::vi2d{-1, -1} : window->GetMousePos();
    }

    olc::HWButton Input::mouse(uint32_t button) const {
        return suppressed > 0 || window == nullptr ? olc::HWButton{} : window->GetMouse(button);
    }

    olc::PixelGameEngine* Input::target() const {
        return window;
    }

    KeyEventQueue& Input::event_queue() {
        return queue;
    }

    InputMode Input::mode() const {
        return input_mode;
    }

    InputDelayStats Input::delay() const {
        return queue.input_delay();
    }

    void Input::reset_delay() {
        queue.reset_input_delay();
    }

    SuppressedInput::SuppressedInput(Input& input, bool active) : input(input), active(active) {
        if (active) {
            input.suppress();
        }
    }

    SuppressedInput::~SuppressedInput() {
        if (active) {
            input.restore();
        }
    }
}
