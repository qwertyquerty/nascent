#pragma once

#include "KeyEvents.h"

#include <string>

namespace nascent {
    bool install_window_hook(KeyEventQueue* queue, const std::string& title);
    void uninstall_window_hook();
}
