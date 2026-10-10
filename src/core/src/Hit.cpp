#include "Hit.h"

namespace nascent {
    void from_json(const nlohmann::json& j, Hit& p) {
        j.at("key").get_to(p.key);
        j.at("hit_type").get_to(p.hit_type);
        j.at("time").get_to(p.time);
        j.at("end_time").get_to(p.end_time);
    }

    void to_json(nlohmann::json& j, const Hit& p) {
        j = nlohmann::json{
            {"key", p.key},
            {"hit_type", p.hit_type},
            {"time", p.time},
            {"end_time", p.end_time}
        };
    }
}
