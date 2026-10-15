#pragma once

#include "gse/input/KeyEvents.h"

#include <string>

namespace nascent {
    bool install_window_hook(KeyEventQueue* queue, const std::string& title);
    void uninstall_window_hook();
    void set_window_hook_key_events(bool enabled);
}
