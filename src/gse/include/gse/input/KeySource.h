#pragma once

#include "gse/input/KeyEvents.h"

#include <optional>

namespace nascent {
    std::optional<InputMode> start_key_source(KeyEventQueue* queue);
    void stop_key_source();
}
