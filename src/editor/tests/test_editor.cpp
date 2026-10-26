#include "TestSupport.h"

#include "ArchiveExport.h"
#include "ChartDocument.h"
#include "OsuExport.h"
#include "PatternGenerator.h"
#include "StepManiaExport.h"
#include "StepManiaFormat.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace nascent;
using namespace nascent::test;

namespace {
    std::filesystem::path scratch_folder(const char* name) {
        std::filesystem::path folder = std::filesystem::temp_directory_path() / "nascent_editor_tests" / name;
        std::error_code ec;
        std::filesystem::remove_all(folder, ec);
        std::filesystem::create_directories(folder, ec);
        return folder;
    }

    std::unique_ptr<ChartDocument> blank(const std::filesystem::path& folder) {
        return ChartDocument::from_audio(folder / "song.mp3");
    }

    bool same_notes(const std::vector<Hit>& a, const std::vector<Hit>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const Hit& x, const Hit& y) {
                   return x.key == y.key && x.hit_type == y.hit_type && std::abs(x.time - y.time) <= 1 && (x.hit_type != HitType::HOLD || std::abs(x.end_time - y.end_time) <= 1);
               });
    }

    std::vector<Hit> imported(std::string_view text) {
        std::string error;
        std::vector<StepManiaChart> charts = stepmania_to_chart_jsons(text, error);
        std::unique_ptr<Chart> chart = charts.empty() ? nullptr : Chart::parse(charts.front().chart, 1.0f, error);
        return chart != nullptr ? chart->unpitched_hit_objects() : std::vector<Hit>{};
    }

    ChartData exporter_chart(uint8_t keys) {
        std::unique_ptr<ChartDocument> document = blank(scratch_folder("exporter"));
        document->change([&](ChartData& data) {
            data.info.title = "Re:Zero / Test";
            data.info.artist = "Artist";
            data.info.mapper = "Mapper";
            data.info.difficulty_name = "Insane";
            data.info.key_count = keys;
            data.timing_points = {{1000, 500.0f, 4}, {5000, 300.0f, 3}};
            data.hits = {tap(0, 750), tap(1, 1000), tap((uint8_t)(keys - 1), 1125), hold(2, 1500, 2500), tap(0, 1500), tap(1, 1500), tap(3, 1666), hold((uint8_t)(keys - 1), 6200, 6800)};

            for (uint8_t key = 0; key < keys; key++) {
                data.hits.push_back(tap(key, 5000 + 75 * key));
            }
        });
        return document->data();
    }
}

NASCENT_TEST(editor_document_editing) {
    std::unique_ptr<ChartDocument> document = blank(scratch_folder("editing"));
    CHECK(document->snap(130, 4) == 125 && document->snap(190, 4) == 250);
    CHECK(document->step(130, 4, 1) == 250 && document->step(130, 4, -1) == 125);
    CHECK(std::abs(document->snap(170, 3) - 500.0 / 3.0) < 1e-6);
    CHECK(document->snap(130, 5) == 100 && document->step(100, 5, 1) == 200);
    CHECK(std::abs(document->snap(80, 7) - 500.0 / 7.0) < 1e-6 && std::abs(document->step(0, 7, 3) - 1500.0 / 7.0) < 1e-6);

    document->change([](ChartData& data) {
        data.timing_points.push_back({1000, 250.0f, 4});
        data.hits = {tap(2, 500), tap(0, 250), tap(0, 250), tap(5, 100), hold(1, 750, 700), hold(3, 1000, 1500)};
    });
    CHECK(document->step(1000, 4, 1) == 1062.5);
    CHECK(same_notes(document->data().hits, {tap(0, 250), tap(2, 500), tap(1, 750), hold(3, 1000, 1500)}));
    CHECK(document->note_at(3, 1200, 20) == std::optional<size_t>(3));
    CHECK(!document->note_at(0, 300, 20).has_value());

    document->change([](ChartData& data) { data.info.key_count = 2; });
    CHECK(document->data().hits.size() == 2);
    CHECK(document->undo() && document->data().hits.size() == 4);
    CHECK(document->redo() && document->data().hits.size() == 2);
    CHECK(document->undo() && document->undo() && document->data().hits.empty() && !document->can_undo());

    document->change([](ChartData& data) { data.hits.push_back(tap(3, 300)); });
    CHECK(!document->can_redo());
}

NASCENT_TEST(editor_save_and_reopen) {
    std::filesystem::path folder = scratch_folder("save");
    std::unique_ptr<ChartDocument> document = blank(folder);
    document->change([](ChartData& data) {
        data.info.title = "Round Trip";
        data.info.artist = "Tester";
        data.info.difficulty_name = "Hard";
        data.timing_points.push_back({1000, 400.0f, 3});
    });
    CHECK(document->suggested_file_name() == "Tester - Round Trip [Hard].json");

    std::string error;
    std::filesystem::path file = folder / document->suggested_file_name();
    CHECK(document->save_as(file, error) && !document->dirty());
    std::unique_ptr<ChartDocument> empty = ChartDocument::open(file, error);
    CHECK(empty != nullptr && empty->data().hits.empty() && empty->data().timing_points.size() == 2);
    CHECK(Chart::load(file, 1.0f, error) == nullptr);

    CHECK(export_osu(document->data(), {}, folder / "empty.osu", error) && export_stepmania(document->data(), folder / "empty.sm", 1, error));

    for (const char* name : {"empty.osu", "empty.sm"}) {
        std::unique_ptr<ChartDocument> reopened = ChartDocument::open(folder / name, error);
        CHECK(reopened != nullptr && reopened->data().hits.empty());
    }

    document->change([](ChartData& data) { data.hits = {tap(0, 0), tap(3, 250), hold(1, 500, 1000), tap(2, 1250)}; });
    CHECK(document->dirty() && document->save(error));
    std::unique_ptr<ChartDocument> reopened = ChartDocument::open(file, error);
    CHECK(reopened != nullptr && !reopened->dirty() && reopened->data().info.title == "Round Trip" && reopened->data().info.audio_file == "song.mp3");
    CHECK(reopened != nullptr && same_notes(reopened->data().hits, document->data().hits));

    std::filesystem::create_directories(folder / "copy");
    CHECK(document->save_as(folder / "copy" / "moved.json", error) && document->data().info.audio_file == "../song.mp3");
}

NASCENT_TEST(editor_exports_round_trip) {
    std::string error;
    std::filesystem::path folder = scratch_folder("exports");

    for (uint8_t keys : {4, 7}) {
        ChartData data = exporter_chart(keys);
        CHECK(export_osu(data, {7.0f, 6.5f}, folder / "export.osu", error));
        CHECK(std::string(chart_to_osu(data, {7.0f, 6.5f})).find("HPDrainRate:6.5") != std::string::npos);
        std::unique_ptr<Chart> chart = Chart::load(folder / "export.osu", 1.0f, error);
        CHECK(chart != nullptr && chart->info.key_count == keys && chart->info.hit_accuracy == 3 && chart->info.artist == "Artist");
        CHECK(chart != nullptr && same_notes(chart->unpitched_hit_objects(), data.hits));
    }

    for (uint8_t keys : {4, 6, 7, 8}) {
        ChartData data = exporter_chart(keys);
        std::optional<std::string> ssc = chart_to_ssc(data, 20, error);
        CHECK(ssc.has_value() && same_notes(imported(*ssc), data.hits));

        if (keys != 7) {
            std::optional<std::string> dwi = chart_to_dwi(data, 9, error);
            CHECK(dwi.has_value() && same_notes(imported(*dwi), data.hits));
        }
    }

    ChartData data = exporter_chart(4);
    std::optional<std::string> sm = chart_to_sm(data, 12, error);
    std::vector<StepManiaChart> charts = sm ? stepmania_to_chart_jsons(*sm, error) : std::vector<StepManiaChart>{};
    CHECK(!charts.empty() && charts.front().meter == 12 && charts.front().chart["info"]["title"] == "Re:Zero / Test" && charts.front().chart["info"]["mapper"] == "Mapper");
    CHECK(sm.has_value() && same_notes(imported(*sm), data.hits));
    CHECK(!chart_to_dwi(exporter_chart(5), 1, error).has_value());
    data.info.key_count = 9;
    CHECK(!chart_to_sm(data, 1, error).has_value());
}

NASCENT_TEST(editor_archive_export) {
    std::filesystem::path folder = scratch_folder("archive");
    std::filesystem::create_directories(folder / "media");
    std::ofstream(folder / "media" / "song.mp3", std::ios::binary) << "not really audio";
    std::ofstream(folder / "media" / "bg.png", std::ios::binary) << "not really an image";
    ChartData data = exporter_chart(4);
    data.info.audio_file = "media/song.mp3";
    data.info.image_file = "media/bg.png";
    std::string error;

    for (ArchiveFormat format : {ArchiveFormat::OSZ, ArchiveFormat::NASCENT}) {
        bool osz = format == ArchiveFormat::OSZ;
        std::filesystem::path target = folder / (osz ? "pack.osz" : "pack.zip");
        std::string chart_name = osz ? "pack.osu" : "pack.json";
        CHECK(export_archive(data, folder, target, format, {}, error));

        mz_zip_archive zip = {};
        CHECK(mz_zip_reader_init_file(&zip, target.string().c_str(), 0) && mz_zip_reader_get_num_files(&zip) == 3);
        CHECK(mz_zip_reader_locate_file(&zip, "song.mp3", nullptr, 0) >= 0 && mz_zip_reader_locate_file(&zip, "bg.png", nullptr, 0) >= 0);
        size_t size = 0;
        char* contents = (char*)mz_zip_reader_extract_file_to_heap(&zip, chart_name.c_str(), &size, 0);
        std::unique_ptr<Chart> chart = contents != nullptr ? Chart::load_text(std::string(contents, size), folder / chart_name, 1.0f, error) : nullptr;
        CHECK(chart != nullptr && chart->info.audio_file == "song.mp3" && chart->unpitched_hit_objects().size() == data.hits.size());
        mz_free(contents);
        mz_zip_reader_end(&zip);
    }

    data.info.audio_file = "missing.mp3";
    CHECK(!export_archive(data, folder, folder / "broken.zip", ArchiveFormat::NASCENT, {}, error));
}

NASCENT_TEST(editor_opens_stepmania_files) {
    std::filesystem::path folder = scratch_folder("open_stepmania");
    ChartData four = exporter_chart(4);
    ChartData eight = exporter_chart(8);
    std::string error;
    std::string single = chart_to_ssc(four, 10, error).value_or("");
    std::string doubles = chart_to_ssc(eight, 14, error).value_or("");
    std::filesystem::path file = folder / "pack.ssc";
    std::ofstream(file, std::ios::binary) << single << "\n"
                                          << doubles.substr(doubles.find("//---------------"));

    CHECK(ChartDocument::is_stepmania(file) && ChartDocument::is_stepmania(folder / "song.DWI") && !ChartDocument::is_stepmania(folder / "song.osu"));
    std::vector<ChartDocument::ChartChoice> choices = ChartDocument::stepmania_charts(file, error);
    CHECK(choices.size() == 2 && choices[0].label.find("dance-single") != std::string::npos && choices[1].label.find("dance-double") != std::string::npos);

    std::unique_ptr<ChartDocument> document = choices.size() == 2 ? ChartDocument::from_json(choices[1].chart, file, error) : nullptr;
    CHECK(document != nullptr && document->data().info.key_count == 8 && document->data().hits.size() == eight.hits.size());
    CHECK(document != nullptr && document->path().empty() && document->dirty() && document->directory() == folder);
    CHECK(document != nullptr && std::abs(document->data().timing_points[1].beat_length - 300.0f) < 0.01f);

    std::ofstream(folder / "song.dwi", std::ios::binary) << chart_to_dwi(four, 5, error).value_or("");
    std::unique_ptr<ChartDocument> dwi = ChartDocument::open(folder / "song.dwi", error);
    CHECK(dwi != nullptr && dwi->data().hits.size() == four.hits.size());

    std::ofstream(folder / "broken.sm", std::ios::binary) << "#TITLE:Nothing;";
    CHECK(ChartDocument::open(folder / "broken.sm", error) == nullptr && !error.empty());
}

NASCENT_TEST(editor_selection_transforms) {
    std::unique_ptr<ChartDocument> document = blank(scratch_folder("selection"));
    document->change([](ChartData& data) { data.hits = {tap(0, 100), tap(1, 200), hold(2, 300, 600), tap(3, 900), tap(1, 400)}; });

    std::vector<Hit> boxed = document->notes_in(1, 2, 150, 450);
    CHECK(boxed.size() == 3 && document->notes_in(2, 2, 500, 550).size() == 1);
    CHECK(document->contains(hold(2, 300, 600)) && !document->contains(tap(2, 300)));

    std::vector<Hit> moved = shifted_hits(boxed, 1000, 1);
    CHECK(same_notes(moved, {tap(2, 1200), hold(3, 1300, 1600), tap(2, 1400)}));
    document->change([&](ChartData& data) { replace_hits(data, boxed, moved); });
    CHECK(document->data().hits.size() == 5 && !document->contains(tap(1, 200)) && document->contains(hold(3, 1300, 1600)));

    document->change([](ChartData& data) { replace_hits(data, {}, {tap(3, 900), hold(0, 100, 150)}); });
    CHECK(document->data().hits.size() == 5 && document->contains(hold(0, 100, 150)) && !document->contains(tap(0, 100)));

    CHECK(same_notes(mirrored_hits({tap(0, 10), hold(3, 20, 40), tap(1, 50)}, 4), {tap(3, 10), hold(0, 20, 40), tap(2, 50)}));
    CHECK(mirrored_hits({tap(3, 0)}, 7).front().key == 3);
    CHECK(same_notes(rotated_hits({tap(0, 10), tap(3, 20), hold(2, 30, 60)}, 4, 1), {tap(1, 10), tap(0, 20), hold(3, 30, 60)}));
    CHECK(same_notes(rotated_hits({tap(0, 10), tap(3, 20)}, 4, -1), {tap(3, 10), tap(2, 20)}));
    CHECK(rotated_hits({tap(1, 0)}, 7, 9).front().key == 3);
}

NASCENT_TEST(editor_stretches_by_beats) {
    std::unique_ptr<ChartDocument> document = blank(scratch_folder("stretch"));
    document->change([](ChartData& data) { data.timing_points = {{0, 500.0f, 4}, {2000, 250.0f, 4}}; });

    CHECK(std::abs(document->beat_at(1000) - 2.0) < 1e-9 && std::abs(document->beat_at(2500) - 6.0) < 1e-9);
    CHECK(std::abs(document->time_at_beat(6.0) - 2500) < 1e-9 && std::abs(document->time_at_beat(-1.0) + 500) < 1e-9);

    std::vector<Hit> eighths = {tap(0, 1000), tap(1, 1250), hold(2, 1500, 1750)};
    std::vector<Hit> quarters = document->stretched(eighths, 2.0, 1000);
    CHECK(same_notes(quarters, {tap(0, 1000), tap(1, 1500), hold(2, 2000, 2250)}));
    CHECK(same_notes(document->stretched(quarters, 0.5, 1000), eighths));

    std::vector<Hit> sloppy = {tap(0, 1010), tap(1, 1240), hold(2, 1490, 1770), hold(3, 1990, 2010)};
    CHECK(same_notes(document->quantized(sloppy, 4, true), {tap(0, 1000), tap(1, 1250), hold(2, 1500, 1750), hold(3, 2000, 2062)}));
    CHECK(same_notes(document->quantized(sloppy, 4, false), {tap(0, 1000), tap(1, 1250), hold(2, 1500, 1780), hold(3, 2000, 2020)}));
}

NASCENT_TEST(editor_generates_patterns) {
    auto valid = [](const std::vector<PatternRow>& rows, size_t count, uint8_t keys) {
        return rows.size() == count && std::all_of(rows.begin(), rows.end(), [&](const PatternRow& row) {
                   return !row.empty() && std::is_sorted(row.begin(), row.end()) && std::adjacent_find(row.begin(), row.end()) == row.end() && row.back() < keys;
               });
    };
    auto shared = [](const PatternRow& a, const PatternRow& b) {
        return std::count_if(a.begin(), a.end(), [&](uint8_t column) { return std::find(b.begin(), b.end(), column) != b.end(); });
    };

    std::vector<PatternSettings> variations(6);
    variations[1].chords = StreamChords::QUADS;
    variations[1].jumps_between = true;
    variations[1].chord_every = 4;
    variations[2].flow = StreamFlow::ZIGZAG;
    variations[2].chords = StreamChords::JUMPS;
    variations[2].anchor = true;
    variations[2].anchor_lane = 5;
    variations[3].disconnect_chance = 0;
    variations[3].jack_chance = 50;
    variations[3].density = 100;
    variations[3].split = SplitStyle::HALVES;
    variations[4].first_lane = 2;
    variations[4].last_lane = 4;
    variations[4].chords = StreamChords::CUSTOM;
    variations[4].chord_size = 9;
    variations[4].kept_lanes = 6;
    variations[4].split = SplitStyle::INNER_OUTER;
    variations[5].density = 1;
    variations[5].vary_sizes = false;
    variations[5].lane_a = 9;

    for (PatternFamily family : {PatternFamily::STREAM, PatternFamily::SINGLE_TRILL, PatternFamily::SPLIT_TRILL, PatternFamily::JACKS}) {
        for (const PatternSettings& settings : variations) {
            for (uint8_t keys = 1; keys <= 10; keys++) {
                CHECK(valid(generate_pattern(family, keys, 48, 7, settings), 48, keys));
            }

            CHECK(generate_pattern(family, 7, 32, 99, settings) == generate_pattern(family, 7, 32, 99, settings));
        }
    }

    PatternSettings connected;
    connected.disconnect_chance = 0;
    PatternSettings disconnected;
    disconnected.disconnect_chance = 100;
    std::vector<PatternRow> skipping = generate_pattern(PatternFamily::STREAM, 7, 64, 3, disconnected);
    PatternSettings jumps;
    jumps.chords = StreamChords::JUMPS;
    std::vector<PatternRow> random = generate_pattern(PatternFamily::STREAM, 4, 64, 3);
    std::vector<PatternRow> flowing = generate_pattern(PatternFamily::STREAM, 7, 64, 3, connected);
    std::vector<PatternRow> jumpstream = generate_pattern(PatternFamily::STREAM, 4, 64, 3, jumps);

    for (size_t row = 1; row < 64; row++) {
        CHECK(shared(random[row], random[row - 1]) == 0);
        CHECK(std::abs(flowing[row][0] - flowing[row - 1][0]) == 1);
        CHECK(std::abs(skipping[row][0] - skipping[row - 1][0]) >= 2);
        CHECK(shared(jumpstream[row], jumpstream[row - 1]) == 0 && jumpstream[row].size() == (row % 2 == 0 ? 2u : 1u));
    }

    auto jacks_in = [&](const std::vector<PatternRow>& rows) {
        size_t count = 0;

        for (size_t row = 1; row < rows.size(); row++) {
            count += shared(rows[row], rows[row - 1]) > 0 ? 1 : 0;
        }

        return count;
    };
    PatternSettings rolling_jumps;
    rolling_jumps.flow = StreamFlow::ROLL;
    rolling_jumps.chords = StreamChords::JUMPS;
    PatternSettings shared_anchor_hands;
    shared_anchor_hands.anchor = true;
    shared_anchor_hands.anchor_alone = false;
    shared_anchor_hands.anchor_lane = 3;
    shared_anchor_hands.chords = StreamChords::HANDS;
    shared_anchor_hands.chord_every = 4;
    CHECK(jacks_in(generate_pattern(PatternFamily::STREAM, 4, 96, 8, rolling_jumps)) == 0);
    CHECK(jacks_in(generate_pattern(PatternFamily::STREAM, 7, 96, 8, shared_anchor_hands)) == 0);
    PatternSettings trills;
    trills.trill_chance = 100;
    std::vector<PatternRow> trilling = generate_pattern(PatternFamily::STREAM, 7, 64, 8, trills);
    std::vector<PatternRow> untrilled = generate_pattern(PatternFamily::STREAM, 7, 64, 8);

    for (size_t row = 2; row < 64; row++) {
        CHECK(trilling[row] == trilling[row - 2]);
        CHECK(untrilled[row] != untrilled[row - 2]);
    }

    PatternSettings jacky;
    jacky.jack_chance = 100;
    CHECK(jacks_in(generate_pattern(PatternFamily::STREAM, 4, 96, 8, jacky)) >= 40);

    PatternSettings roll;
    roll.flow = StreamFlow::ROLL;
    CHECK(generate_pattern(PatternFamily::STREAM, 4, 6, 0, roll) == std::vector<PatternRow>({{0}, {1}, {2}, {3}, {0}, {1}}));
    roll.reverse = true;
    roll.first_lane = 4;
    roll.last_lane = 6;
    CHECK(generate_pattern(PatternFamily::STREAM, 7, 4, 0, roll) == std::vector<PatternRow>({{6}, {5}, {4}, {6}}));
    roll.flow = StreamFlow::ZIGZAG;
    roll.reverse = false;
    roll.first_lane = 0;
    roll.last_lane = 255;
    CHECK(generate_pattern(PatternFamily::STREAM, 4, 7, 0, roll) == std::vector<PatternRow>({{0}, {1}, {2}, {3}, {2}, {1}, {0}}));

    PatternSettings anchored;
    anchored.anchor = true;
    anchored.anchor_lane = 2;
    anchored.anchor_every = 3;
    std::vector<PatternRow> anchor_rows = generate_pattern(PatternFamily::STREAM, 6, 30, 5, anchored);

    anchored.flow = StreamFlow::ROLL;
    anchored.chords = StreamChords::HANDS;
    std::vector<PatternRow> anchored_hands = generate_pattern(PatternFamily::STREAM, 6, 30, 5, anchored);
    anchored.anchor_alone = false;
    anchored.anchor_every = 4;
    std::vector<PatternRow> shared_anchor = generate_pattern(PatternFamily::STREAM, 6, 90, 5, anchored);
    bool anchor_in_stream = false;

    for (size_t row = 0; row < anchor_rows.size(); row++) {
        CHECK((row % 3 == 0) == (anchor_rows[row] == PatternRow{2}));
        CHECK((row % 3 == 0) == (std::find(anchored_hands[row].begin(), anchored_hands[row].end(), 2) != anchored_hands[row].end()));
    }

    for (size_t row = 0; row < shared_anchor.size(); row++) {
        CHECK(row % 4 != 0 || shared_anchor[row] == PatternRow{2});
        anchor_in_stream = anchor_in_stream || (row % 4 != 0 && std::find(shared_anchor[row].begin(), shared_anchor[row].end(), 2) != shared_anchor[row].end());
    }

    CHECK(anchor_in_stream);

    PatternSettings trill;
    trill.lane_a = 1;
    trill.lane_b = 3;
    CHECK(generate_pattern(PatternFamily::SINGLE_TRILL, 4, 3, 0, trill) == std::vector<PatternRow>({{1}, {3}, {1}}));
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 4, 2, 0, trill) == std::vector<PatternRow>({{0, 2}, {1, 3}}));
    trill.split = SplitStyle::HALVES;
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 7, 2, 0, trill) == std::vector<PatternRow>({{0, 1, 2, 3}, {4, 5, 6}}));
    trill.split = SplitStyle::INNER_OUTER;
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 4, 2, 0, trill) == std::vector<PatternRow>({{0, 3}, {1, 2}}));
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 7, 2, 0, trill) == std::vector<PatternRow>({{0, 1, 5, 6}, {2, 3, 4}}));
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 5, 2, 0, trill) == std::vector<PatternRow>({{0, 4}, {1, 2, 3}}));
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 2, 2, 0, trill) == std::vector<PatternRow>({{0}, {1}}));
    CHECK(generate_pattern(PatternFamily::SPLIT_TRILL, 1, 2, 0, trill) == std::vector<PatternRow>({{0}, {0}}));

    PatternSettings quadjack;
    quadjack.density = 100;
    quadjack.vary_sizes = false;
    CHECK(generate_pattern(PatternFamily::JACKS, 4, 2, 0, quadjack) == std::vector<PatternRow>({{0, 1, 2, 3}, {0, 1, 2, 3}}));
    PatternSettings single_lane;
    single_lane.density = 1;
    single_lane.vary_sizes = false;
    std::vector<PatternRow> jack = generate_pattern(PatternFamily::JACKS, 7, 16, 4, single_lane);
    CHECK(std::all_of(jack.begin(), jack.end(), [&](const PatternRow& row) { return row == jack.front() && row.size() == 1; }));
    PatternSettings jumpjack;
    jumpjack.density = 50;
    jumpjack.vary_sizes = false;
    std::vector<PatternRow> jumpjacks = generate_pattern(PatternFamily::JACKS, 4, 32, 4, jumpjack);

    for (size_t row = 1; row < jumpjacks.size(); row++) {
        CHECK(jumpjacks[row].size() == 2 && shared(jumpjacks[row], jumpjacks[row - 1]) == 1);
    }
}
