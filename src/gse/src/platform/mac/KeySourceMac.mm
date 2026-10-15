#include "gse/input/KeySource.h"

#import <AppKit/AppKit.h>
#include <Carbon/Carbon.h>

#include <algorithm>

namespace nascent {
    static id key_monitor = nil;
    static id resign_observer = nil;
    static KeyEventQueue* key_queue = nullptr;

    static olc::Key key_from_key_code(unsigned short code) {
        switch (code) {
            case kVK_ANSI_A:
                return olc::Key::A;
            case kVK_ANSI_B:
                return olc::Key::B;
            case kVK_ANSI_C:
                return olc::Key::C;
            case kVK_ANSI_D:
                return olc::Key::D;
            case kVK_ANSI_E:
                return olc::Key::E;
            case kVK_ANSI_F:
                return olc::Key::F;
            case kVK_ANSI_G:
                return olc::Key::G;
            case kVK_ANSI_H:
                return olc::Key::H;
            case kVK_ANSI_I:
                return olc::Key::I;
            case kVK_ANSI_J:
                return olc::Key::J;
            case kVK_ANSI_K:
                return olc::Key::K;
            case kVK_ANSI_L:
                return olc::Key::L;
            case kVK_ANSI_M:
                return olc::Key::M;
            case kVK_ANSI_N:
                return olc::Key::N;
            case kVK_ANSI_O:
                return olc::Key::O;
            case kVK_ANSI_P:
                return olc::Key::P;
            case kVK_ANSI_Q:
                return olc::Key::Q;
            case kVK_ANSI_R:
                return olc::Key::R;
            case kVK_ANSI_S:
                return olc::Key::S;
            case kVK_ANSI_T:
                return olc::Key::T;
            case kVK_ANSI_U:
                return olc::Key::U;
            case kVK_ANSI_V:
                return olc::Key::V;
            case kVK_ANSI_W:
                return olc::Key::W;
            case kVK_ANSI_X:
                return olc::Key::X;
            case kVK_ANSI_Y:
                return olc::Key::Y;
            case kVK_ANSI_Z:
                return olc::Key::Z;
            case kVK_ANSI_0:
                return olc::Key::K0;
            case kVK_ANSI_1:
                return olc::Key::K1;
            case kVK_ANSI_2:
                return olc::Key::K2;
            case kVK_ANSI_3:
                return olc::Key::K3;
            case kVK_ANSI_4:
                return olc::Key::K4;
            case kVK_ANSI_5:
                return olc::Key::K5;
            case kVK_ANSI_6:
                return olc::Key::K6;
            case kVK_ANSI_7:
                return olc::Key::K7;
            case kVK_ANSI_8:
                return olc::Key::K8;
            case kVK_ANSI_9:
                return olc::Key::K9;
            case kVK_ANSI_Keypad0:
                return olc::Key::NP0;
            case kVK_ANSI_Keypad1:
                return olc::Key::NP1;
            case kVK_ANSI_Keypad2:
                return olc::Key::NP2;
            case kVK_ANSI_Keypad3:
                return olc::Key::NP3;
            case kVK_ANSI_Keypad4:
                return olc::Key::NP4;
            case kVK_ANSI_Keypad5:
                return olc::Key::NP5;
            case kVK_ANSI_Keypad6:
                return olc::Key::NP6;
            case kVK_ANSI_Keypad7:
                return olc::Key::NP7;
            case kVK_ANSI_Keypad8:
                return olc::Key::NP8;
            case kVK_ANSI_Keypad9:
                return olc::Key::NP9;
            case kVK_ANSI_KeypadMultiply:
                return olc::Key::NP_MUL;
            case kVK_ANSI_KeypadPlus:
                return olc::Key::NP_ADD;
            case kVK_ANSI_KeypadDivide:
                return olc::Key::NP_DIV;
            case kVK_ANSI_KeypadMinus:
                return olc::Key::NP_SUB;
            case kVK_ANSI_KeypadDecimal:
                return olc::Key::NP_DECIMAL;
            case kVK_ANSI_KeypadEnter:
            case kVK_Return:
                return olc::Key::ENTER;
            case kVK_F1:
                return olc::Key::F1;
            case kVK_F2:
                return olc::Key::F2;
            case kVK_F3:
                return olc::Key::F3;
            case kVK_F4:
                return olc::Key::F4;
            case kVK_F5:
                return olc::Key::F5;
            case kVK_F6:
                return olc::Key::F6;
            case kVK_F7:
                return olc::Key::F7;
            case kVK_F8:
                return olc::Key::F8;
            case kVK_F9:
                return olc::Key::F9;
            case kVK_F10:
                return olc::Key::F10;
            case kVK_F11:
                return olc::Key::F11;
            case kVK_F12:
                return olc::Key::F12;
            case kVK_UpArrow:
                return olc::Key::UP;
            case kVK_DownArrow:
                return olc::Key::DOWN;
            case kVK_LeftArrow:
                return olc::Key::LEFT;
            case kVK_RightArrow:
                return olc::Key::RIGHT;
            case kVK_Delete:
                return olc::Key::BACK;
            case kVK_ForwardDelete:
                return olc::Key::DEL;
            case kVK_Escape:
                return olc::Key::ESCAPE;
            case kVK_Tab:
                return olc::Key::TAB;
            case kVK_Home:
                return olc::Key::HOME;
            case kVK_End:
                return olc::Key::END;
            case kVK_PageUp:
                return olc::Key::PGUP;
            case kVK_PageDown:
                return olc::Key::PGDN;
            case kVK_Help:
                return olc::Key::INS;
            case kVK_Shift:
            case kVK_RightShift:
                return olc::Key::SHIFT;
            case kVK_Control:
            case kVK_RightControl:
                return olc::Key::CTRL;
            case kVK_CapsLock:
                return olc::Key::CAPS_LOCK;
            case kVK_Space:
                return olc::Key::SPACE;
            case kVK_ANSI_Semicolon:
                return olc::Key::OEM_1;
            case kVK_ANSI_Slash:
                return olc::Key::OEM_2;
            case kVK_ANSI_Grave:
                return olc::Key::OEM_3;
            case kVK_ANSI_LeftBracket:
                return olc::Key::OEM_4;
            case kVK_ANSI_Backslash:
                return olc::Key::OEM_5;
            case kVK_ANSI_RightBracket:
                return olc::Key::OEM_6;
            case kVK_ANSI_Quote:
                return olc::Key::OEM_7;
            case kVK_ANSI_Equal:
                return olc::Key::EQUALS;
            case kVK_ANSI_Comma:
                return olc::Key::COMMA;
            case kVK_ANSI_Minus:
                return olc::Key::MINUS;
            case kVK_ANSI_Period:
                return olc::Key::PERIOD;
            default:
                return olc::Key::NONE;
        }
    }

    static NSEventModifierFlags modifier_flag(olc::Key key) {
        switch (key) {
            case olc::Key::SHIFT:
                return NSEventModifierFlagShift;
            case olc::Key::CTRL:
                return NSEventModifierFlagControl;
            case olc::Key::CAPS_LOCK:
                return NSEventModifierFlagCapsLock;
            default:
                return 0;
        }
    }

    static double event_time_ms(NSEvent* event) {
        double age_ms = ([NSProcessInfo processInfo].systemUptime - event.timestamp) * 1000.0;
        return now_ms() - std::max(age_ms, 0.0);
    }

    static void handle_key_event(NSEvent* event) {
        if (key_queue == nullptr) {
            return;
        }

        olc::Key key = key_from_key_code(event.keyCode);
        double time = event_time_ms(event);

        if (event.type == NSEventTypeFlagsChanged) {
            NSEventModifierFlags flag = modifier_flag(key);

            if (flag != 0) {
                key_queue->push(key, (event.modifierFlags & flag) != 0, time);
            }

            return;
        }

        key_queue->push(key, event.type == NSEventTypeKeyDown, time);
    }

    static void install_monitor() {
        NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged;
        key_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                            handler:^NSEvent*(NSEvent* event) {
                                                              handle_key_event(event);
                                                              return event;
                                                            }];
        resign_observer = [[NSNotificationCenter defaultCenter] addObserverForName:NSApplicationDidResignActiveNotification
                                                                            object:nil
                                                                             queue:nil
                                                                        usingBlock:^(NSNotification*) {
                                                                          if (key_queue != nullptr) {
                                                                              key_queue->release_all(now_ms());
                                                                          }
                                                                        }];
    }

    static void remove_monitor() {
        if (key_monitor != nil) {
            [NSEvent removeMonitor:key_monitor];
            key_monitor = nil;
        }

        if (resign_observer != nil) {
            [[NSNotificationCenter defaultCenter] removeObserver:resign_observer];
            resign_observer = nil;
        }
    }

    static void on_main_thread(void (^block)()) {
        if ([NSThread isMainThread]) {
            block();
        }
        else {
            dispatch_sync(dispatch_get_main_queue(), block);
        }
    }

    std::optional<InputMode> start_key_source(KeyEventQueue* queue) {
        on_main_thread(^{
          key_queue = queue;

          if (key_monitor == nil) {
              install_monitor();
          }
        });

        if (key_monitor == nil) {
            key_queue = nullptr;
            return std::nullopt;
        }

        return InputMode::COCOA_EVENTS;
    }

    void stop_key_source() {
        on_main_thread(^{
          key_queue = nullptr;
          remove_monitor();
        });
    }
}
