#include "Attempt.h"
#include "Chart.h"
#include "Replay.h"
#include "Results.h"
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

NASCENT_TEST(replays) {
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
    Replay replay = make_replay(attempt, summary, {"Pack/chart.json", chart->gameplay_hash(), "Title", "Artist", "Hard", 4}, 1.0f, {10, -5, 210, true}, "2026-10-09T07:40:00Z");

    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_replay_tests";
    std::filesystem::remove_all(directory);
    std::string error;
    std::optional<std::filesystem::path> path = save_replay(replay, directory, error);
    CHECK(path && path->parent_path().filename() == chart->gameplay_hash().substr(0, Replay::FOLDER_HASH_LENGTH));
    CHECK(path && path->filename() == "20261009T074000Z.json");

    std::optional<Replay> loaded = path ? load_replay(*path, error) : std::nullopt;
    CHECK(loaded.has_value());

    if (loaded) {
        CHECK(loaded->chart.gameplay_hash == chart->gameplay_hash());
        CHECK(loaded->settings.mirror);
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

NASCENT_TEST(replays_from_before_gameplay_hashes) {
    nlohmann::json old = {
        {"version", Replay::FORMAT_VERSION},
        {"played_at", "2026-10-01T10:00:00Z"},
        {"chart", {{"path", "Pack/chart.osu"}, {"sha256", "0123456789abcdef0123"}, {"title", "Title"}, {"artist", "Artist"}, {"difficulty_name", "Hard"}, {"key_count", 4}}},
        {"rate", 1.0},
        {"settings", {{"audio_input_offset", 0}, {"audio_visual_offset", 0}, {"scroll_speed", 160}}},
        {"end_time", 3000},
        {"result", {{"accuracy", 1.0}, {"grade", "X"}, {"max_combo", 1}, {"score_counts", {{"PERFECT", 1}}}, {"average_error", 0.0}, {"difficulty", 0.0}, {"performance", 0.0}}},
        {"inputs", {{1000, 0, 1}, {1050, 0, 0}}},
    };

    Replay replay = old.get<Replay>();
    CHECK(replay.chart.gameplay_hash.empty());
    CHECK(!replay.settings.mirror);
    CHECK(replay.inputs.size() == 2);
    CHECK(replay_folder("replays", "") == std::filesystem::path("replays") / "unknown");
    CHECK(replay_folder("replays", std::string(64, 'a')) == std::filesystem::path("replays") / std::string(Replay::FOLDER_HASH_LENGTH, 'a'));
}
