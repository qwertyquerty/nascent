#include "Chart.h"
#include "DifficultyCache.h"
#include "DifficultyBasis.h"
#include "DifficultyCalculator.h"
#include "DifficultyModel.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

static std::vector<Hit> stream(int32_t interval, bool jack) {
    std::vector<Hit> hits;

    for (int32_t time = 0, index = 0; time < 30000; time += interval, index++) {
        hits.push_back(tap(jack ? 0 : (uint8_t)(index % 4), time));
    }

    return hits;
}

NASCENT_TEST(difficulty) {
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

NASCENT_TEST(difficulty_breakdown) {
    auto top = [](const DifficultyBreakdown& breakdown) {
        return (DifficultySkill)(std::max_element(breakdown.mix.begin(), breakdown.mix.end()) - breakdown.mix.begin());
    };

    std::vector<Hit> roll = stream(125, false);
    DifficultyBreakdown breakdown = DifficultyCalculator::breakdown(roll, 4, {}, 5);
    CHECK(breakdown.rating == DifficultyCalculator::calculate(roll, 4, {}, 5));
    double total = 0;

    for (size_t skill = 0; skill < DIFFICULTY_SKILL_COUNT; skill++) {
        total += breakdown.mix[skill];
        CHECK(breakdown.mix[skill] >= 0.0);
        CHECK(breakdown.ratings[skill] <= breakdown.rating + 1e-9);
    }

    CHECK(std::abs(total - 1.0) < 1e-9);
    CHECK(top(breakdown) == DifficultySkill::STREAM);
    CHECK(top(DifficultyCalculator::breakdown(stream(125, true), 4, {}, 5)) == DifficultySkill::JACKSPEED);
    CHECK(breakdown.sustained_seconds > 0.0 && breakdown.sustained_seconds <= 31.0);
    CHECK(DifficultyCalculator::breakdown({}, 4, {}, 5).rating == 0.0);
}

NASCENT_TEST(difficulty_model_matches_features) {
    std::set<std::string> names;

    for (size_t index = 0; index < BASIS_SIZE; index++) {
        names.insert(basis_name(index));
    }

    for (const difficulty_model::NamedWeight& weight : difficulty_model::WEIGHTS) {
        CHECK(names.contains(std::string(weight.name)));
    }
}

NASCENT_TEST(difficulty_scale) {
    CHECK(DifficultyCalculator::calculate({}, 4, {}, 5) == 0.0);
    double single = DifficultyCalculator::calculate({tap(0, 1000)}, 4, {}, 5);
    CHECK(single > 0.5 && single < 1.5);
    CHECK(DifficultyCalculator::calculate(stream(63, false), 7, {}, 5) > 0.0);
    CHECK(DifficultyCalculator::calculate(stream(63, false), 5, {}, 5) > 0.0);
}

static std::filesystem::path cache_path(const std::string& name) {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_difficulty_cache_tests";
    std::filesystem::create_directories(directory);
    std::filesystem::path path = directory / (name + ".json");
    std::filesystem::remove(path);
    return path;
}

NASCENT_TEST(difficulty_keys) {
    std::vector<Hit> hits = stream(125, false);
    std::string error;
    std::unique_ptr<Chart> chart = make_chart(hits);

    nlohmann::json renamed = chart_json(hits);
    renamed["info"]["title"] = "Another Title";
    renamed["info"]["hit_accuracy"] = 2;
    CHECK(Chart::parse(renamed, 1.5f, error)->difficulty_key() == chart->difficulty_key());

    nlohmann::json retimed = chart_json(hits);
    retimed["timing_points"][0]["beat_length"] = 400.0;
    std::unique_ptr<Chart> faster_bpm = Chart::parse(retimed, 1.0f, error);
    CHECK(faster_bpm->gameplay_hash() == chart->gameplay_hash());
    CHECK(faster_bpm->difficulty_key() != chart->difficulty_key());

    std::vector<Hit> moved = hits;
    moved[3].time += 1;
    CHECK(make_chart(moved)->difficulty_key() != chart->difficulty_key());
}

NASCENT_TEST(difficulty_cache) {
    std::filesystem::path path = cache_path("roundtrip");
    std::unique_ptr<Chart> chart = make_chart(stream(125, false));
    std::unique_ptr<Chart> other = make_chart(stream(100, false));
    double expected = DifficultyCalculator::calculate_at_pitch(*chart, 1.2f);

    DifficultyCache cache(path);
    cache.load();
    CHECK(!cache.lookup(*chart, 1.2f));
    CHECK(cache.difficulty(*chart, 1.2f) == expected);
    CHECK(cache.lookup(*chart, 1.2f) == expected);
    CHECK(!cache.lookup(*chart, 1.0f));
    cache.store(*other, 1.0f, 12.5);

    std::string error;
    CHECK(cache.save(error));

    DifficultyCache reloaded(path);
    reloaded.load();
    CHECK(reloaded.lookup(*chart, 1.2f) == expected);
    CHECK(reloaded.lookup(*other, 1.0f) == 12.5);

    reloaded.retain({chart->difficulty_key()});
    CHECK(reloaded.lookup(*chart, 1.2f) == expected);
    CHECK(!reloaded.lookup(*other, 1.0f));
    CHECK(reloaded.size() == 1);
}

NASCENT_TEST(difficulty_cache_rejects_other_calculators) {
    std::filesystem::path path = cache_path("fingerprint");
    std::unique_ptr<Chart> chart = make_chart(stream(125, false));
    nlohmann::json stale = {{"version", DifficultyCache::FILE_VERSION}, {"fingerprint", "an older calculator"}, {"charts", {{chart->difficulty_key(), {{"100", 999.0}}}}}};
    std::ofstream(path) << stale.dump();

    DifficultyCache cache(path);
    cache.load();
    CHECK(!cache.lookup(*chart, 1.0f));
    CHECK(cache.difficulty(*chart, 1.0f) == DifficultyCalculator::calculate_at_pitch(*chart, 1.0f));
    CHECK(!DifficultyCache::fingerprint().empty());
}
