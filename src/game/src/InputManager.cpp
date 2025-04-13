#include "InputManager.h"

namespace nascent {
    uint32_t InputManager::get_lane_keys_pressed(olc::PixelGameEngine* window) {
        uint32_t keys = 0;
        keys |= (window->GetKey(olc::Key::Z).bHeld) ? (1 << 0) : 0;
        keys |= (window->GetKey(olc::Key::X).bHeld) ? (1 << 1) : 0;
        keys |= (window->GetKey(olc::Key::PERIOD).bHeld) ? (1 << 2) : 0;
        keys |= (window->GetKey(olc::Key::OEM_2).bHeld) ? (1 << 3) : 0;
        return keys;
    }
}