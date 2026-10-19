#pragma once

#include "gse/input/KeyEvents.h"

#include <optional>
#include <string>

namespace nascent {
    std::optional<InputMode> start_key_source(KeyEventQueue* queue, const std::string& title);
    void stop_key_source();
}
