#pragma once

#include <cstdint>
#include "json.h"
using json = nlohmann::json;

namespace nascent {
    enum HitType {
        HIT,
        HOLD
    };
    
    struct Hit {
        uint8_t key;
        HitType hit_type;
        int32_t time;
        int32_t end_time;
    };

    void to_json(json& j, const Hit& p);
    void from_json(const json& j, Hit& p);
}
