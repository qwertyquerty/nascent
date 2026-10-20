#include "Chart.h"
#include "Const.h"
#include "DifficultyCalculator.h"
#include "OsuFormat.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

NASCENT_TEST(chart_infinite_beat_length) {
    std::vector<Hit> hits;

    for (int32_t time = 0; time < 20000; time += 125) {
        hits.push_back(tap((uint8_t)(time / 125 % 4), time));
    }

    nlohmann::json data = chart_json(hits);
    data["timing_points"].push_back({{"time", 5000}, {"beat_length", nullptr}, {"meter", 4}});
    std::string error;
    std::unique_ptr<Chart> chart = Chart::parse(data, 1.0f, error);
    CHECK(chart != nullptr);
    CHECK(std::isinf(chart->timing_points.back().beat_length));
    CHECK(std::isfinite(DifficultyCalculator::calculate(*chart)));
    CHECK(DifficultyCalculator::calculate(*chart) > 0.0);
}

NASCENT_TEST(chart_loading) {
    std::unique_ptr<Chart> chart = make_chart({tap(1, 2000), tap(0, 1000), hold(2, 1500, 4000)});
    CHECK(chart->hits[0].time == 1000);
    CHECK(chart->hits[2].time == 2000);
    CHECK(chart->get_duration_ms() == 3000);

    std::string error;
    CHECK(Chart::parse(chart_json({tap(4, 1000)}), 1.0f, error) == nullptr);
    CHECK(Chart::parse(chart_json({}), 1.0f, error) == nullptr);
    CHECK(Chart::parse(chart_json({hold(0, 1000, 500)}), 1.0f, error) == nullptr);
    CHECK(Chart::parse(nlohmann::json::object(), 1.0f, error) == nullptr);

    std::unique_ptr<Chart> fast = make_chart({tap(0, 1000), hold(1, 2000, 3000)}, 2.0f);
    CHECK(fast->hits[0].time == 500);
    CHECK(fast->hits[1].end_time == 1500);
    CHECK(std::abs(fast->timing_points[0].beat_length - 250.0f) < 0.001f);
}

NASCENT_TEST(gameplay_hash) {
    std::vector<Hit> hits = {tap(0, 1000), hold(1, 1000, 1500), tap(2, 2000)};
    std::string error;
    std::string hash = make_chart(hits)->gameplay_hash();
    CHECK(hash.size() == 64);

    nlohmann::json renamed = chart_json({hits[2], hits[1], hits[0]});
    renamed["info"]["title"] = "Another Title";
    CHECK(Chart::parse(renamed, 1.5f, error)->gameplay_hash() == hash);

    std::vector<Hit> moved = hits;
    moved[2].time = 2001;
    CHECK(make_chart(moved)->gameplay_hash() != hash);
    CHECK(Chart::parse(chart_json(hits, 9), 1.0f, error)->gameplay_hash() == hash);

    std::unique_ptr<Chart> chart = make_chart(hits);
    CHECK(Chart::parse(chart->serialize(), 1.0f, error)->gameplay_hash() == hash);
}

NASCENT_TEST(osu_conversion) {
    std::string text =
        "osu file format v14\n\n[General]\nAudioFilename: audio.mp3\nMode: 3\n\n[Metadata]\nTitle:Song\nArtist:Someone\nCreator:Mapper\nVersion:Hard\n\n"
        "[Difficulty]\nCircleSize:4\nOverallDifficulty:8\n\n[Events]\n0,0,\"bg, one.jpg\",0,0\n\n"
        "[TimingPoints]\n0,500,4,1,0,100,1,0\n1000,-100,4,1,0,100,0,0\n\n[HitObjects]\n448,192,2000,1,0,0:0:0:0:\n64,192,1000,128,0,1500:0:0:0:0:\n";
    std::string error;
    std::optional<nlohmann::json> converted = osu_to_chart_json(text, error);
    CHECK(converted.has_value());

    if (!converted) {
        return;
    }

    std::unique_ptr<Chart> chart = Chart::parse(*converted, 1.0f, error);
    CHECK(chart != nullptr);

    if (chart == nullptr) {
        return;
    }

    CHECK(chart->info.key_count == 4);
    CHECK(chart->info.image_file == "bg, one.jpg");
    CHECK(chart->timing_points.size() == 1);
    CHECK(chart->hits[0].key == 0 && chart->hits[0].hit_type == HitType::HOLD && chart->hits[0].end_time == 1500);
    CHECK(chart->hits[1].key == 3 && chart->hits[1].time == 2000);

    std::string mode_error;
    CHECK(!osu_to_chart_json("osu file format v14\n[General]\nMode: 0\n", mode_error).has_value());
    CHECK(mode_error == "not an osu!mania beatmap");
}

NASCENT_TEST(bpm_range) {
    ChartInfo info;
    info.key_count = 4;
    std::vector<Hit> hits = {{0, HitType::HIT, 1000, 1000}, {1, HitType::HIT, 11000, 11000}};
    std::string error;
    std::unique_ptr<Chart> chart = Chart::create(info, hits, {{0, 500.0f, 4}, {3000, 375.0f, 4}, {20000, 100.0f, 4}}, error);
    CHECK(chart != nullptr);

    if (chart) {
        BpmRange range = chart->bpm_range();
        CHECK(std::abs(range.main - 160.0) < 0.01);
        CHECK(std::abs(range.min - 120.0) < 0.01);
        CHECK(std::abs(range.max - 160.0) < 0.01);
    }

    std::unique_ptr<Chart> steady = Chart::create(info, hits, {{0, 500.0f, 4}}, error);
    CHECK(steady && std::abs(steady->bpm_range().main - 120.0) < 0.01 && steady->bpm_range().min == steady->bpm_range().max);
}

NASCENT_TEST(intro_skip) {
    auto chart_with_first_note = [](int32_t time) {
        ChartInfo info;
        info.key_count = 4;
        std::vector<Hit> hits = {{0, HitType::HIT, time, time}, {1, HitType::HIT, time + 500, time + 500}};
        std::string error;
        return Chart::create(info, std::move(hits), {{0, 500.0f, 4}}, error);
    };

    std::unique_ptr<Chart> long_intro = chart_with_first_note(12000);
    std::unique_ptr<Chart> short_intro = chart_with_first_note(INTRO_SKIP_MIN_GAP_MS);
    CHECK(long_intro && long_intro->intro_skip_ms() == 12000 - INTRO_SKIP_LEAD_MS);
    CHECK(short_intro && short_intro->intro_skip_ms() == 0);

    if (long_intro) {
        long_intro->set_pitch(1.5f);
        CHECK(long_intro->intro_skip_ms() == 8000 - INTRO_SKIP_LEAD_MS);
    }
}

NASCENT_TEST(snap_pitch) {
    float pitch = 1.0f;

    for (int i = 0; i < 10; i++) {
        pitch = snap_pitch(pitch + PITCH_INTERVAL);
    }

    CHECK(pitch == 1.5f);
    CHECK(snap_pitch(1.0000004768f) == 1.0f);
    CHECK(snap_pitch(1.12f) == 1.1f);
    CHECK(snap_pitch(0.1f) == PITCH_MINIMUM);
    CHECK(snap_pitch(9.0f) == PITCH_MAXIMUM);
}
