#include "gse/input/WindowHook.h"
#include "gse/Platform.h"

namespace nascent {
    bool install_window_hook(KeyEventQueue*, const std::string&) {
        return false;
    }

    void uninstall_window_hook() {
    }

    void set_window_hook_key_events(bool) {
    }

    void set_cursor_hidden(bool) {
    }

    bool window_placement_supported() {
        return false;
    }

    std::vector<DisplayInfo> list_displays() {
        return {};
    }

    bool place_window(WindowMode, const std::string&, int32_t, int32_t) {
        return false;
    }
}
