#include "Attempt.h"
#include "Chart.h"
#include "Const.h"
#include "DifficultyCalculator.h"
#include "Mp3Padding.h"
#include "OsuFormat.h"
#include "Replay.h"
#include "Results.h"
#include "SettingsStore.h"
#include "Sha256.h"
#include "SongClock.h"

#include "json.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace nascent;

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr << std::endl; \
            failures++; \
        } \
    } while (0)

static Hit tap(uint8_t key, int32_t time) {
    return {key, HitType::HIT, time, 0};
}

static Hit hold(uint8_t key, int32_t time, int32_t end_time) {
    return {key, HitType::HOLD, time, end_time};
}

static nlohmann::json chart_json(const std::vector<Hit>& hits, uint8_t hit_accuracy = 10) {
    return {
        {"info", {{"title", "Test"}, {"title_unicode", "Test"}, {"artist", "Artist"}, {"artist_unicode", "Artist"}, {"mapper", "Mapper"}, {"difficulty_name", "Normal"}, {"audio_file", "audio.mp3"}, {"image_file", ""}, {"audio_lead_in", 1000}, {"preview_time", 5000}, {"hit_accuracy", hit_accuracy}, {"key_count", 4}, {"damage", 5}}},
        {"timing_points", {{{"time", 0}, {"beat_length", 500.0}, {"meter", 4}}}},
        {"hit_objects", hits}
    };
}

static std::unique_ptr<Chart> make_chart(const std::vector<Hit>& hits, float pitch = 1.0f) {
    std::string error;
    std::unique_ptr<Chart> chart = Chart::parse(chart_json(hits), pitch, error);

    if (chart == nullptr) {
        std::cerr << "make_chart failed: " << error << std::endl;
        std::exit(1);
    }

    return chart;
}

static void test_chart_loading() {
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

static void test_judging() {
    CHECK(get_window_from_hit_acc_diff(HitScore::PERFECT, 10) == 10);
    CHECK(get_window_from_hit_acc_diff(HitScore::BAD, 0) == MAX_BAD_WINDOW_SIZE);
    CHECK(get_score_from_hit_err(11, 10, false) == HitScore::AMAZING);
    CHECK(get_score_from_hit_err(161, 10, false) == HitScore::NONE);

    std::unique_ptr<Chart> chart = make_chart({tap(0, 1000), tap(1, 2000), tap(2, 3000), tap(3, 3450), tap(3, 3600), hold(0, 5000, 6000), hold(1, 7000, 8000)});
    Attempt attempt(chart.get());

    attempt.press(0, 500);
    CHECK(!attempt.judged_hits[0].hit);

    attempt.release(0, 600);
    attempt.press(0, 1005);
    attempt.release(0, 1050);
    CHECK(attempt.judged_hits[0].hit_score == HitScore::PERFECT);
    CHECK(attempt.judged_hits[0].hit_err == 5);

    attempt.update(2500);
    CHECK(attempt.judged_hits[1].hit_score == HitScore::MISS);
    CHECK(attempt.get_combo() == 0);

    attempt.press(2, 2950);
    CHECK(attempt.judged_hits[2].hit_score == HitScore::GREAT);

    attempt.press(3, 3595);
    CHECK(!attempt.judged_hits[3].hit);
    CHECK(attempt.judged_hits[4].hit_score == HitScore::PERFECT);
    attempt.release(3, 3650);

    attempt.press(0, 5000);
    attempt.release(0, 6010);
    CHECK(attempt.judged_hits[5].release_score == HitScore::PERFECT);

    attempt.press(1, 7000);
    attempt.update(8500);
    CHECK(attempt.judged_hits[6].released);
    CHECK(!attempt.judged_hits[6].release_played);
    CHECK(attempt.judged_hits[6].release_score == HitScore::MISS);
}

static void test_gameplay_hash() {
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

static void test_osu_conversion() {
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

static void test_replays() {
    std::unique_ptr<Chart> chart = make_chart({tap(0, 1000), tap(1, 1500), tap(2, 2000), hold(3, 2500, 3500), tap(0, 4000), hold(1, 4500, 5000)});
    Attempt attempt(chart.get());

    attempt.press(0, 1012);
    attempt.update(1100);
    attempt.release(0, 1080);
    attempt.press(2, 1960);
    attempt.press(3, 2490);
    attempt.release(2, 2050);
    attempt.release(3, 3480);
    attempt.press(0, 4030);
    attempt.press(1, 4500);
    attempt.update(5600);
    attempt.release(0, 4100);
    attempt.update(6000);

    AttemptSummary summary = summarize_attempt(attempt, 42.0);
    Replay replay = make_replay(attempt, summary, {"Pack/chart.json", sha256_hex("abc"), "Title", "Artist", "Hard", 4}, 1.0f, {10, -5, 210}, "2026-10-09T07:40:00Z");

    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_replay_tests";
    std::filesystem::remove_all(directory);
    std::string error;
    std::optional<std::filesystem::path> path = save_replay(replay, directory, error);
    CHECK(path && path->parent_path().filename() == "ba7816bf8f01cfea");
    CHECK(path && path->filename() == "20261009T074000Z.json");

    std::optional<Replay> loaded = path ? load_replay(*path, error) : std::nullopt;
    CHECK(loaded.has_value());

    if (loaded) {
        Attempt replayed = simulate_replay(*chart, *loaded);
        CHECK(replayed.get_accuracy() == attempt.get_accuracy());
        CHECK(replayed.max_combo == attempt.max_combo);

        for (size_t i = 0; i < attempt.judged_hits.size(); i++) {
            CHECK(replayed.judged_hits[i].hit_err == attempt.judged_hits[i].hit_err);
            CHECK(replayed.judged_hits[i].release_score == attempt.judged_hits[i].release_score);
        }
    }

    std::filesystem::remove_all(directory);
}

struct TestSettings {
    int32_t speed = 100;
    std::string name = "default";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TestSettings, speed, name)

static std::filesystem::path settings_file(const std::string& name, const std::string& text) {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_settings_tests";
    std::filesystem::create_directories(directory);
    std::filesystem::path path = directory / (name + ".json");
    std::filesystem::remove(directory / (name + ".invalid.json"));
    std::ofstream(path, std::ios::trunc) << text;
    return path;
}

static nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    return nlohmann::json::parse(ifs, nullptr, false);
}

static void test_settings() {
    std::string error;
    std::map<uint32_t, VersionedJsonFile::Migration> migrations = {
        {1, [](nlohmann::json& data) {
             data["speed"] = data["old_speed"].get<int32_t>() * 2;
             data.erase("old_speed");
         }}
    };

    std::filesystem::path migrated = settings_file("migrate", R"({"version": 1, "old_speed": 21, "future": "kept"})");
    SettingsStore<TestSettings> store(migrated, 2, migrations);
    CHECK(store.load(error) == SettingsLoadStatus::MIGRATED);
    CHECK(store.value.speed == 42);
    CHECK(read_json(migrated)["version"] == 2);
    CHECK(read_json(migrated)["future"] == "kept");

    std::filesystem::path newer = settings_file("newer", R"({"version": 9, "speed": 77})");
    SettingsStore<TestSettings> newer_store(newer, 1);
    CHECK(newer_store.load(error) == SettingsLoadStatus::NEWER_VERSION);
    CHECK(!newer_store.save(error));
    CHECK(read_json(newer)["version"] == 9);

    std::filesystem::path invalid = settings_file("invalid", "not json {");
    SettingsStore<TestSettings> invalid_store(invalid, 1);
    CHECK(invalid_store.load(error) == SettingsLoadStatus::INVALID);
    CHECK(std::filesystem::exists(invalid_store.get_file().get_backup_path()));
    CHECK(read_json(invalid)["speed"] == 100);
}

static std::vector<Hit> stream(int32_t interval, bool jack) {
    std::vector<Hit> hits;

    for (int32_t time = 0, index = 0; time < 30000; time += interval, index++) {
        hits.push_back(tap(jack ? 0 : (uint8_t)(index % 4), time));
    }

    return hits;
}

static void test_difficulty() {
    double slow = DifficultyCalculator::calculate(stream(125, false), 4, {}, 5);
    CHECK(slow > 0.0);
    CHECK(DifficultyCalculator::calculate(stream(63, false), 4, {}, 5) > slow);
    CHECK(DifficultyCalculator::calculate(stream(125, true), 4, {}, 5) > slow);
    CHECK(DifficultyCalculator::calculate(stream(125, false), 4, {}, 6) > slow);

    std::unique_ptr<Chart> chart = make_chart(stream(125, false));
    double normal = DifficultyCalculator::calculate(*chart);
    chart->set_pitch(1.2f);
    CHECK(DifficultyCalculator::calculate(*chart) > normal);
}

static void test_results() {
    CHECK(grade_for_accuracy(1.0) == Grade::X);
    CHECK(grade_for_accuracy(0.98) == Grade::S);
    CHECK(grade_for_accuracy(0.9799) == Grade::A);
    CHECK(grade_for_accuracy(0.7499) == Grade::F);
    CHECK(std::abs(performance_for(80.0, 0.95) - 60.0) < 1e-9);
    CHECK(performance_for(80.0, 0.80) == 0.0);

    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_song_clock() {
    for (double period : {10.0, 21.3, 33.3}) {
        for (double frame : {2.0, 6.9, 16.7}) {
            SongClock clock;
            double callback_time = 0;
            double cursor = 0;
            double next_callback = 0;
            double lowest = 1e9;
            double highest = -1e9;
            int32_t step = 0;

            for (double now = 0; now < 30000; now += frame) {
                while (next_callback <= now) {
                    callback_time = next_callback;
                    cursor = next_callback + period;
                    next_callback += period + (step++ % 2 == 0 ? 0.5 : -0.5);
                }

                clock.sync(std::floor(cursor) + (now - callback_time), now);

                if (now > 2000) {
                    double error = clock.position_at(now) - now;
                    lowest = std::min(lowest, error);
                    highest = std::max(highest, error);
                }
            }

            CHECK(clock.snaps == 0);
            CHECK(highest - lowest < 2.0);
        }
    }
}

static void test_snap_pitch() {
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

static std::vector<unsigned char> mp3_with_tag(const char* tag, const char* encoder, uint32_t delay) {
    std::vector<unsigned char> data = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 20};
    data.resize(data.size() + 20, 0);
    data.insert(data.end(), {0xFF, 0xFB, 0x90, 0x00});
    data.resize(data.size() + 32, 0);

    if (tag == nullptr) {
        data.resize(data.size() + 400, 0);
        return data;
    }

    data.insert(data.end(), tag, tag + 4);
    data.insert(data.end(), {0, 0, 0, 0x0F});
    data.resize(data.size() + 4 + 4 + 100 + 4, 0);
    size_t encoder_offset = data.size();
    data.resize(data.size() + 36, 0);
    std::copy(encoder, encoder + 4, data.begin() + (std::ptrdiff_t)encoder_offset);
    data[encoder_offset + 21] = (unsigned char)(delay >> 4);
    data[encoder_offset + 22] = (unsigned char)((delay & 0x0F) << 4);
    return data;
}

static void test_mp3_padding() {
    std::optional<Mp3Padding> lame = parse_mp3_padding(mp3_with_tag("Info", "LAME", 576));
    CHECK(lame && lame->sample_rate == 44100 && lame->samples == 1152 + 576 + MP3_DECODER_DELAY_SAMPLES);

    std::optional<Mp3Padding> lavf = parse_mp3_padding(mp3_with_tag("Xing", "Lavf", 1105));
    CHECK(lavf && lavf->samples == 1152 + 1105 + MP3_DECODER_DELAY_SAMPLES);

    std::optional<Mp3Padding> lavc = parse_mp3_padding(mp3_with_tag("Info", "Lavc", 0));
    CHECK(lavc && lavc->samples == 1152);

    std::optional<Mp3Padding> untagged = parse_mp3_padding(mp3_with_tag(nullptr, nullptr, 0));
    CHECK(untagged && untagged->samples == 0 && untagged->sample_rate == 44100);

    CHECK(std::abs(lame->milliseconds() - 2257.0 * 1000.0 / 44100.0) < 1e-9);
    CHECK(!parse_mp3_padding(std::vector<unsigned char>(64, 0)));
}

int main() {
    std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"chart_loading", test_chart_loading},
        {"judging", test_judging},
        {"gameplay_hash", test_gameplay_hash},
        {"osu_conversion", test_osu_conversion},
        {"replays", test_replays},
        {"settings", test_settings},
        {"difficulty", test_difficulty},
        {"results", test_results},
        {"snap_pitch", test_snap_pitch},
        {"song_clock", test_song_clock},
        {"mp3_padding", test_mp3_padding},
    };

    for (const auto& [name, test] : tests) {
        int before = failures;
        test();
        std::cout << (failures == before ? "PASS " : "FAIL ") << name << std::endl;
    }

    std::cout << (failures ? "FAILED" : "OK") << " (" << failures << " failed checks)" << std::endl;
    return failures ? 1 : 0;
}
