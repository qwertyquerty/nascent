#pragma once

#include <cstdint>

#include "json.h"

namespace nascent {
    enum class HitType : uint8_t {
        HIT,
        HOLD
    };

    struct Hit {
        uint8_t key = 0;
        HitType hit_type = HitType::HIT;
        int32_t time = 0;
        int32_t end_time = 0;
    };

    void to_json(nlohmann::json& j, const Hit& p);
    void from_json(const nlohmann::json& j, Hit& p);
}
