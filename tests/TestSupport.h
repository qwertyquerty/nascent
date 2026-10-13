#pragma once

#include "Chart.h"
#include "Hit.h"

#include "json.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace nascent::test {
    inline int failures = 0;

    struct TestCase {
        const char* name;
        std::function<void()> run;
    };

    inline std::vector<TestCase>& registry() {
        static std::vector<TestCase> tests;
        return tests;
    }

    struct Registration {
        Registration(const char* name, std::function<void()> run) {
            registry().push_back({name, std::move(run)});
        }
    };

    inline Hit tap(uint8_t key, int32_t time) {
        return {key, HitType::HIT, time, 0};
    }

    inline Hit hold(uint8_t key, int32_t time, int32_t end_time) {
        return {key, HitType::HOLD, time, end_time};
    }

    inline nlohmann::json chart_json(const std::vector<Hit>& hits, uint8_t hit_accuracy = 10) {
        return {
            {"info", {{"title", "Test"}, {"title_unicode", "Test"}, {"artist", "Artist"}, {"artist_unicode", "Artist"}, {"mapper", "Mapper"}, {"difficulty_name", "Normal"}, {"audio_file", "audio.mp3"}, {"image_file", ""}, {"audio_lead_in", 1000}, {"preview_time", 5000}, {"hit_accuracy", hit_accuracy}, {"key_count", 4}, {"damage", 5}}},
            {"timing_points", {{{"time", 0}, {"beat_length", 500.0}, {"meter", 4}}}},
            {"hit_objects", hits}
        };
    }

    inline std::unique_ptr<Chart> make_chart(const std::vector<Hit>& hits, float pitch = 1.0f) {
        std::string error;
        std::unique_ptr<Chart> chart = Chart::parse(chart_json(hits), pitch, error);

        if (chart == nullptr) {
            std::cerr << "make_chart failed: " << error << std::endl;
            std::exit(1);
        }

        return chart;
    }
}

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr << std::endl; \
            nascent::test::failures++; \
        } \
    } while (0)

#define NASCENT_TEST_CONCAT_INNER(a, b) a##b
#define NASCENT_TEST_CONCAT(a, b) NASCENT_TEST_CONCAT_INNER(a, b)
#define NASCENT_TEST(name) \
    static void NASCENT_TEST_CONCAT(test_, name)(); \
    static nascent::test::Registration NASCENT_TEST_CONCAT(name, _registration)(#name, NASCENT_TEST_CONCAT(test_, name)); \
    static void NASCENT_TEST_CONCAT(test_, name)()
