#include "Accuracy.h"
#include "Attempt.h"
#include "Chart.h"
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
