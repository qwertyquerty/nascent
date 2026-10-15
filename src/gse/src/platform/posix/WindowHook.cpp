#include "gse/input/WindowHook.h"

namespace nascent {
    bool install_window_hook(KeyEventQueue*, const std::string&) {
        return false;
    }

    void uninstall_window_hook() {
    }

    void set_window_hook_key_events(bool) {
    }
}
