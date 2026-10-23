#include "Accuracy.h"
#include "Attempt.h"
#include "Chart.h"
#include "Replay.h"
#include "Results.h"
#include "TestSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

NASCENT_TEST(error_deviation) {
    std::unique_ptr<Chart> chart = make_chart({tap(0, 1000), tap(1, 2000)});
    Attempt attempt(chart.get());
    CHECK(attempt.get_err_deviation() == 0);

    attempt.press(0, 1010);
    attempt.release(0, 1050);
    attempt.press(1, 1990);
    attempt.release(1, 2050);
    CHECK(attempt.get_avg_err() == 0);
    CHECK(std::abs(attempt.get_err_deviation() - 10.0) < 1e-9);
}

NASCENT_TEST(judging) {
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

static std::optional<uint32_t> expected_press_target(const Attempt& attempt, uint8_t key, int32_t time) {
    std::optional<uint32_t> first_candidate;

    for (uint32_t i = attempt.soonest_passed_hit_index; i < attempt.judged_hits.size(); i++) {
        const JudgedHit& jhit = attempt.judged_hits[i];

        if (jhit.chart_hit.key != key || jhit.hit) {
            continue;
        }

        HitScore score = get_score_from_hit_err(std::abs(time - jhit.chart_hit.time), attempt.hit_accuracy, false);

        if (score == HitScore::NONE) {
            break;
        }

        if (!first_candidate) {
            first_candidate = i;
        }

        if (score != HitScore::BAD) {
            return i;
        }
    }

    return first_candidate;
}

NASCENT_TEST(empty_lane_presses) {
    std::mt19937 random(7);
    std::vector<Hit> hits;

    for (int32_t time = 1000; time < 40000; time += 20 + (int32_t)(random() % 160)) {
        hits.push_back(tap((uint8_t)(random() % 3), time));
    }

    std::unique_ptr<Chart> chart = make_chart(hits);
    Attempt attempt(chart.get());
    std::array<int32_t, 4> released_at = {0, 0, 0, 0};

    for (int32_t time = 900; time < 40500; time += 5 + (int32_t)(random() % 60)) {
        for (uint8_t key = 0; key < 4; key++) {
            if ((attempt.held_keys & (1u << key)) && time >= released_at[key]) {
                attempt.release(key, time);
            }
        }

        uint8_t key = (uint8_t)(random() % 4);

        if (attempt.held_keys & (1u << key)) {
            continue;
        }

        attempt.update(time);
        uint32_t scored = attempt.n_scored_notes;
        std::optional<uint32_t> target = expected_press_target(attempt, key, time);
        attempt.press(key, time);
        released_at[key] = time + 10 + (int32_t)(random() % 80);

        CHECK(attempt.n_scored_notes == scored + (target ? 1u : 0u));

        if (target) {
            const JudgedHit& jhit = attempt.judged_hits[*target];
            CHECK(jhit.hit && jhit.hit_played);
            CHECK(jhit.hit_err == time - jhit.chart_hit.time);
        }

        if (key == 3) {
            CHECK(!target);
        }
    }

    attempt.update(41000);

    std::array<uint32_t, HIT_SCORE_COUNT> counts = {};

    for (const JudgedHit& jhit : attempt.judged_hits) {
        if (jhit.hit) {
            counts[(size_t)jhit.hit_score]++;
        }

        if (jhit.released) {
            counts[(size_t)jhit.release_score]++;
        }
    }

    CHECK(counts == attempt.score_counts);

    AttemptSummary summary = summarize_attempt(attempt, 0.0);
    Replay replay = make_replay(attempt, summary, {"Pack/chart.json", chart->gameplay_hash(), "Title", "Artist", "Hard", 4}, 1.0f, {}, "2026-10-22T00:00:00Z");
    Attempt replayed = simulate_replay(*chart, replay);
    CHECK(replayed.score_counts == attempt.score_counts);
    CHECK(replayed.get_accuracy() == attempt.get_accuracy());
    CHECK(replayed.max_combo == attempt.max_combo);

    for (size_t i = 0; i < attempt.judged_hits.size(); i++) {
        CHECK(replayed.judged_hits[i].hit_played == attempt.judged_hits[i].hit_played);
        CHECK(replayed.judged_hits[i].hit_score == attempt.judged_hits[i].hit_score);
        CHECK(replayed.judged_hits[i].hit_err == attempt.judged_hits[i].hit_err);
    }
}
