#include "ChartSearch.h"
#include "DifficultyCache.h"
#include "LibraryCache.h"
#include "SettingsStore.h"
#include "TestSupport.h"
#include "Utf8Path.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

static std::filesystem::path library_cache_directory(const std::string& name) {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_tests" / ("library_cache_" + name);
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);
    return directory;
}

static std::unique_ptr<Chart> library_chart(const std::filesystem::path& path, const std::vector<Hit>& hits) {
    std::string error;
    std::unique_ptr<Chart> chart = Chart::load_text(chart_json(hits).dump(), path, 1.0f, error);

    if (chart == nullptr) {
        std::cerr << "library_chart failed: " << error << std::endl;
        std::exit(1);
    }

    return chart;
}

static bool same_hits(const std::vector<Hit>& a, const std::vector<Hit>& b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t index = 0; index < a.size(); index++) {
        if (a[index].key != b[index].key || a[index].hit_type != b[index].hit_type || a[index].time != b[index].time || a[index].end_time != b[index].end_time) {
            return false;
        }
    }

    return true;
}

static const LibraryCache::Restored* restored_for(const std::vector<LibraryCache::Restored>& restored, const std::string& hash) {
    for (const LibraryCache::Restored& entry : restored) {
        if (entry.chart->gameplay_hash() == hash) {
            return &entry;
        }
    }

    return nullptr;
}

NASCENT_TEST(library_cache_round_trip) {
    std::filesystem::path directory = library_cache_directory("round_trip");
    std::filesystem::path first_path = directory / "song" / "first.json";
    std::unique_ptr<Chart> first = library_chart(first_path, {tap(0, 1000), hold(1, 1500, 2600), tap(3, 3000)});

    LibraryCache writer(directory);
    std::string error;
    CHECK(writer.rewrite({{path_utf8(first_path), 120, 5, 7, {first.get()}, false}, {path_utf8(directory / "song" / "other.osu"), 40, 6, 7, {}, false}}, error));

    LibraryCache reader(directory);
    std::vector<LibraryCache::Restored> restored = reader.load(1.25f);
    CHECK(restored.size() == 1);
    CHECK(reader.size() == 2);
    CHECK(reader.chart_count() == 1);

    const LibraryCache::Restored* entry = restored_for(restored, first->gameplay_hash());
    CHECK(entry != nullptr);

    if (entry == nullptr) {
        return;
    }

    ChartSummary expected = summarize_chart(*first);
    CHECK(!entry->chart->has_notes());
    CHECK(entry->chart->pitch == 1.25f);
    CHECK(entry->added == 7);
    CHECK(entry->summary.notes == expected.notes && entry->summary.holds == expected.holds);
    CHECK(entry->summary.start_ms == expected.start_ms && entry->summary.end_ms == expected.end_ms);
    CHECK(entry->summary.bpm.main == expected.bpm.main && entry->summary.bpm.min == expected.bpm.min && entry->summary.bpm.max == expected.bpm.max);
    CHECK(entry->chart->difficulty_key() == first->difficulty_key());
    CHECK(entry->chart->unpitched_chart_info().title == first->unpitched_chart_info().title);
    CHECK(entry->chart->unpitched_chart_info().preview_time == first->unpitched_chart_info().preview_time);
    CHECK(entry->chart->unpitched_timing().size() == first->unpitched_timing().size());
    CHECK(entry->chart->unpitched_timing().front().beat_length == first->unpitched_timing().front().beat_length);

    std::optional<std::vector<Hit>> notes = reader.load_notes(*entry->chart);
    CHECK(notes && same_hits(*notes, first->unpitched_hit_objects()));

    entry->chart->restore_notes(std::move(*notes));
    CHECK(entry->chart->has_notes());
    CHECK(entry->chart->get_chart_end_ms() == (int32_t)std::lround(3000 / 1.25f));
}

NASCENT_TEST(library_cache_keeps_unchanged_files) {
    std::filesystem::path directory = library_cache_directory("keeps");
    std::filesystem::path first_path = directory / "a" / "first.json";
    std::filesystem::path second_path = directory / "b" / "second.json";
    std::unique_ptr<Chart> first = library_chart(first_path, {tap(0, 1000), tap(1, 2000)});
    std::unique_ptr<Chart> second = library_chart(second_path, {hold(2, 500, 900), tap(3, 1200)});
    std::string error;

    {
        LibraryCache cache(directory);
        CHECK(cache.rewrite({{path_utf8(first_path), 1, 1, 1, {first.get()}, false}}, error));
    }

    LibraryCache cache(directory);
    CHECK(cache.load(1.0f).size() == 1);
    CHECK(cache.rewrite({{path_utf8(first_path), 1, 1, 1, {}, true}, {path_utf8(second_path), 2, 2, 2, {second.get()}, false}}, error));

    LibraryCache reader(directory);
    std::vector<LibraryCache::Restored> restored = reader.load(1.0f);
    CHECK(restored.size() == 2);

    for (const Chart* original : {first.get(), second.get()}) {
        const LibraryCache::Restored* entry = restored_for(restored, original->gameplay_hash());
        CHECK(entry != nullptr);

        if (entry != nullptr) {
            std::optional<std::vector<Hit>> notes = reader.load_notes(*entry->chart);
            CHECK(notes && same_hits(*notes, original->unpitched_hit_objects()));
        }
    }

    CHECK(reader.rewrite({{path_utf8(second_path), 2, 2, 2, {}, true}}, error));
    LibraryCache shrunk(directory);
    CHECK(shrunk.load(1.0f).size() == 1);
    CHECK(shrunk.find(path_utf8(first_path)) == nullptr);
}

NASCENT_TEST(library_cache_compacts) {
    std::filesystem::path directory = library_cache_directory("compacts");
    std::filesystem::path kept_path = directory / "kept" / "chart.json";
    std::filesystem::path churn_path = directory / "churn" / "chart.json";
    std::vector<Hit> kept_hits;

    for (int32_t index = 0; index < 200; index++) {
        kept_hits.push_back(tap((uint8_t)(index % 4), 1000 + index * 100));
    }

    std::unique_ptr<Chart> kept = library_chart(kept_path, kept_hits);
    LibraryCache cache(directory);
    cache.compact_min_bytes = 0;
    std::string error;
    CHECK(cache.rewrite({{path_utf8(kept_path), 1, 1, 1, {kept.get()}, false}}, error));

    for (int32_t round = 0; round < 6; round++) {
        std::vector<Hit> churn_hits;

        for (int32_t index = 0; index < 300; index++) {
            churn_hits.push_back(tap((uint8_t)(index % 4), 1000 + index * 50 + round));
        }

        std::unique_ptr<Chart> churn = library_chart(churn_path, churn_hits);
        CHECK(cache.rewrite({{path_utf8(kept_path), 1, 1, 1, {}, true}, {path_utf8(churn_path), (uint64_t)round, round, 1, {churn.get()}, false}}, error));
    }

    std::error_code ec;
    uint64_t size = std::filesystem::file_size(directory / LibraryCache::NOTES_FILE_NAME, ec);
    CHECK(size < 200 * 11 + 300 * 11 * 3);

    LibraryCache reader(directory);
    std::vector<LibraryCache::Restored> restored = reader.load(1.0f);
    CHECK(restored.size() == 2);
    const LibraryCache::Restored* entry = restored_for(restored, kept->gameplay_hash());
    CHECK(entry != nullptr);

    if (entry != nullptr) {
        std::optional<std::vector<Hit>> notes = reader.load_notes(*entry->chart);
        CHECK(notes && same_hits(*notes, kept_hits));
    }
}

static void legacy_value(std::string& out, const void* data, size_t size) {
    out.append((const char*)data, size);
}

template <typename T>
static void legacy_number(std::string& out, T value) {
    legacy_value(out, &value, sizeof(T));
}

static void legacy_text(std::string& out, const std::string& text) {
    legacy_number(out, (uint32_t)text.size());
    out.append(text);
}

NASCENT_TEST(library_cache_migrates_legacy_file) {
    std::filesystem::path directory = library_cache_directory("legacy");
    std::filesystem::path path = directory / "song" / "chart.json";
    std::unique_ptr<Chart> chart = library_chart(path, {tap(0, 400), hold(2, 800, 1600)});
    const ChartInfo& info = chart->unpitched_chart_info();

    std::string body;
    legacy_text(body, path_utf8(path));
    legacy_number(body, (uint64_t)10);
    legacy_number(body, (int64_t)20);
    legacy_number(body, (int64_t)30);
    legacy_number(body, (uint8_t)1);

    for (const std::string* text : {&info.title, &info.title_unicode, &info.artist, &info.artist_unicode, &info.mapper, &info.difficulty_name, &info.audio_file, &info.image_file}) {
        legacy_text(body, *text);
    }

    legacy_number(body, info.audio_lead_in);
    legacy_number(body, info.preview_time);
    legacy_number(body, info.hit_accuracy);
    legacy_number(body, info.key_count);
    legacy_number(body, info.damage);
    legacy_text(body, chart->gameplay_hash());
    legacy_text(body, chart->difficulty_key());
    legacy_number(body, (uint32_t)chart->unpitched_hit_objects().size());

    for (const Hit& hit : chart->unpitched_hit_objects()) {
        legacy_number(body, hit.key);
        legacy_number(body, (uint8_t)hit.hit_type);
        legacy_number(body, hit.time);
        legacy_number(body, hit.end_time);
    }

    legacy_number(body, (uint32_t)chart->unpitched_timing().size());

    for (const ChartTimingPoint& point : chart->unpitched_timing()) {
        legacy_number(body, point.time);
        legacy_number(body, point.beat_length);
        legacy_number(body, point.meter);
    }

    std::string file(LibraryCache::LEGACY_MAGIC);
    legacy_number(file, LibraryCache::LEGACY_FORMAT);
    legacy_text(file, std::format("{}\n{}", Chart::CHART_HASH_FORMAT, LibraryCache::LEGACY_FORMAT));
    legacy_number(file, (uint32_t)1);
    legacy_number(file, (uint32_t)body.size());
    file.append(body);
    std::ofstream(directory / LibraryCache::LEGACY_FILE_NAME, std::ios::binary) << file;

    LibraryCache cache(directory);
    std::vector<LibraryCache::Restored> restored = cache.load(1.0f);
    CHECK(restored.size() == 1);
    CHECK(!std::filesystem::exists(directory / LibraryCache::LEGACY_FILE_NAME));
    CHECK(std::filesystem::exists(directory / LibraryCache::INDEX_FILE_NAME));

    if (!restored.empty()) {
        CHECK(restored[0].added == 30);
        CHECK(restored[0].summary.holds == 1);
        std::optional<std::vector<Hit>> notes = cache.load_notes(*restored[0].chart);
        CHECK(notes && same_hits(*notes, chart->unpitched_hit_objects()));
    }

    const LibraryCache::Record* record = cache.find(path_utf8(path));
    CHECK(record != nullptr && record->size == 10 && record->modified == 20);
}

NASCENT_TEST(difficulty_cache_migrates_legacy_json) {
    std::filesystem::path directory = library_cache_directory("difficulty");
    std::unique_ptr<Chart> chart = make_chart({tap(0, 1000), tap(1, 1500)});
    nlohmann::json legacy = {{"version", DifficultyCache::LEGACY_FILE_VERSION}, {"fingerprint", DifficultyCache::fingerprint()}, {"charts", {{chart->difficulty_key(), {{"100", 12.5}, {"150", 20.25}}}}}};
    std::ofstream(directory / "difficulty_cache.json") << legacy.dump();

    DifficultyCache cache(directory / "difficulty_cache.bin");
    cache.load();
    CHECK(cache.lookup(*chart, 1.0f) == 12.5);
    CHECK(cache.lookup(*chart, 1.5f) == 20.25);
    std::string error;
    CHECK(cache.save(error));
    CHECK(!std::filesystem::exists(directory / "difficulty_cache.json"));

    DifficultyCache reloaded(directory / "difficulty_cache.bin");
    reloaded.load();
    CHECK(reloaded.lookup(*chart, 1.5f) == 20.25);
    CHECK(reloaded.size() == 2);
}

NASCENT_TEST(binary_json_file_round_trip) {
    std::filesystem::path path = library_cache_directory("binary_json") / "data.bin";
    nlohmann::json value = {{"text", "hello"}, {"numbers", {1, 2, 3}}, {"blob", nlohmann::json::binary({1, 2, 255})}};
    std::string error;
    CHECK(BinaryJsonFile(path, 3).save(value, error));

    nlohmann::json loaded;
    CHECK(BinaryJsonFile(path, 3).load(loaded));
    CHECK(loaded == value);
    CHECK(!BinaryJsonFile(path, 4).load(loaded));
}

NASCENT_TEST(search_query_narrows) {
    CHECK(query_narrows(parse_search("ether"), parse_search("ether s")));
    CHECK(query_narrows(parse_search("eth"), parse_search("ether")));
    CHECK(!query_narrows(parse_search("ether"), parse_search("eth")));
    CHECK(!query_narrows(parse_search("diff>1"), parse_search("diff>10")));
    CHECK(!query_narrows(parse_search("keys"), parse_search("keys=4")));
    CHECK(query_narrows(parse_search("keys=4 a"), parse_search("keys=4 ab")));
}
