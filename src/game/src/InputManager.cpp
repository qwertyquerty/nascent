#include "InputManager.h"

namespace nascent {
    uint32_t InputManager::get_lane_keys_pressed(olc::PixelGameEngine* window, uint8_t key_count) {
        uint32_t keys = 0;

        keys |= (window->GetKey(olc::Key::Z).bHeld) ? (1 << 0) : 0;
        keys |= (window->GetKey(olc::Key::X).bHeld) ? (1 << 1) : 0;

        if (key_count == 4) {
            keys |= (window->GetKey(olc::Key::PERIOD).bHeld) ? (1 << 2) : 0;
            keys |= (window->GetKey(olc::Key::OEM_2).bHeld) ? (1 << 3) : 0;
        }
        else if (key_count == 7) {
            keys |= (window->GetKey(olc::Key::C).bHeld) ? (1 << 2) : 0;
            keys |= (window->GetKey(olc::Key::SPACE).bHeld) ? (1 << 3) : 0;
            keys |= (window->GetKey(olc::Key::COMMA).bHeld) ? (1 << 4) : 0;
            keys |= (window->GetKey(olc::Key::PERIOD).bHeld) ? (1 << 5) : 0;
            keys |= (window->GetKey(olc::Key::OEM_2).bHeld) ? (1 << 6) : 0;     
        }
    
        return keys;
    }
}
