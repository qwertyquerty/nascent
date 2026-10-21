#include "Chart.h"
#include "StepManiaFormat.h"
#include "TestSupport.h"

#include <memory>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

namespace {
    std::vector<int32_t> hit_times(const nlohmann::json& chart) {
        std::vector<int32_t> times;

        for (const nlohmann::json& hit : chart.at("hit_objects")) {
            times.push_back(hit.at("time").get<int32_t>());
        }

        return times;
    }

    std::vector<int32_t> timing_times(const nlohmann::json& chart) {
        std::vector<int32_t> times;

        for (const nlohmann::json& point : chart.at("timing_points")) {
            times.push_back(point.at("time").get<int32_t>());
        }

        return times;
    }
}

NASCENT_TEST(stepmania_sm_conversion) {
    std::string text =
        "#TITLE:Re:Start;\n#SUBTITLE:(Remix);\n#ARTIST:Someone;\n#MUSIC:song.ogg;\n#BACKGROUND:bg.png;\n#OFFSET:-0.100;\n#BPMS:0.000=120.000;\n"
        "// a comment\n"
        "#NOTES:\n     dance-single:\n     Author:\n     Hard:\n     12:\n     0,0,0,0,0:\n1000\n0100\n0010\n000M\n,\n2000\n0000\n3000\n0001\n;\n"
        "#NOTES:\n     dance-double:\n     Crazy:\n     Edit:\n     15:\n     0,0,0,0,0:\n00000001\n;\n"
        "#NOTES:\n     lights-cabinet:\n     :\n     Easy:\n     1:\n     0,0,0,0,0:\n00000000\n;\n";
    std::string error;
    std::vector<StepManiaChart> charts = stepmania_to_chart_jsons(text, error);
    CHECK(charts.size() == 3);

    if (charts.size() != 3) {
        return;
    }

    const nlohmann::json& single = charts[0].chart;
    CHECK(charts[0].error.empty());
    CHECK(charts[0].steps_type == "dance-single" && charts[0].difficulty == "Hard" && charts[0].meter == 12);
    CHECK(single["info"]["title"] == "Re:Start (Remix)");
    CHECK(single["info"]["mapper"] == "Author");
    CHECK(single["info"]["audio_file"] == "song.ogg");
    CHECK(single["info"]["key_count"] == 4);
    CHECK((hit_times(single) == std::vector<int32_t>{100, 600, 1100, 2100, 3600}));
    CHECK(single["hit_objects"][3]["hit_type"] == 1 && single["hit_objects"][3]["end_time"] == 3100);
    CHECK(single["hit_objects"][4]["key"] == 3);

    std::unique_ptr<Chart> chart = Chart::parse(single, 1.0f, error);
    CHECK(chart != nullptr);

    CHECK(charts[1].chart["info"]["key_count"] == 8);
    CHECK(charts[1].chart["info"]["difficulty_name"] == "Crazy");
    CHECK(charts[1].chart["hit_objects"][0]["key"] == 7);

    CHECK(charts[2].chart.is_null());
    CHECK(charts[2].error == "steps type 'lights-cabinet' is not supported");
}

NASCENT_TEST(stepmania_stops_delays_warps_fakes) {
    std::string text =
        "#OFFSET:0;\n#BPMS:0=60;\n#STOPS:1=0.5;\n#DELAYS:3=0.25;\n#WARPS:5=1;\n#FAKES:7=0.5;\n"
        "#NOTES:dance-single::Easy:1::\n1000\n1000\n1000\n1000\n,\n1000\n1000\n1000\n1000\n,\n1000\n;\n";
    std::string error;
    std::vector<StepManiaChart> charts = stepmania_to_chart_jsons(text, error);
    CHECK(charts.size() == 1);

    if (charts.size() != 1) {
        return;
    }

    CHECK(charts[0].error.empty());
    CHECK((hit_times(charts[0].chart) == std::vector<int32_t>{0, 1000, 2500, 3750, 4750, 5750, 7750}));
    CHECK((timing_times(charts[0].chart) == std::vector<int32_t>{0, 1500, 3750, 5750}));
}

NASCENT_TEST(stepmania_ssc_chart_timing) {
    std::string text =
        "#VERSION:0.83;\n#TITLE:Song;\n#OFFSET:0;\n#BPMS:0=120;\n"
        "#NOTEDATA:;\n#STEPSTYPE:dance-single;\n#DIFFICULTY:Challenge;\n#METER:14;\n#CREDIT:Charter;\n#BPMS:0=60;\n#NOTES:\n1000\n0100\n;\n"
        "#NOTEDATA:;\n#STEPSTYPE:dance-single;\n#DIFFICULTY:Medium;\n#METER:5;\n#NOTES:\n1000\n0100\n;\n";
    std::string error;
    std::vector<StepManiaChart> charts = stepmania_to_chart_jsons(text, error);
    CHECK(charts.size() == 2);

    if (charts.size() != 2) {
        return;
    }

    CHECK(charts[0].difficulty == "Challenge" && charts[0].meter == 14);
    CHECK(charts[0].chart["info"]["mapper"] == "Charter");
    CHECK((hit_times(charts[0].chart) == std::vector<int32_t>{0, 2000}));
    CHECK(charts[1].difficulty == "Medium");
    CHECK((hit_times(charts[1].chart) == std::vector<int32_t>{0, 1000}));
}

NASCENT_TEST(stepmania_recovery_and_errors) {
    std::string text = "#TITLE:Caf\xE9\n#ARTIST:X;\n#BPMS:0=60,1.5=120;\n#NOTES:dance-single::Hard:3::1000\n0000\n0100\n0000\n;\n";
    std::string error;
    std::vector<StepManiaChart> charts = stepmania_to_chart_jsons(text, error);
    CHECK(charts.size() == 1);

    if (charts.size() == 1) {
        CHECK(charts[0].chart["info"]["title"] == "Caf\xC3\xA9");
        CHECK(charts[0].chart["info"]["artist"] == "X");
        CHECK((hit_times(charts[0].chart) == std::vector<int32_t>{0, 1750}));
        CHECK((timing_times(charts[0].chart) == std::vector<int32_t>{0, 1250}));
    }

    std::string not_stepmania;
    CHECK(stepmania_to_chart_jsons("hello", not_stepmania).empty());
    CHECK(not_stepmania == "not a StepMania file");

    std::string negative;
    std::vector<StepManiaChart> rejected = stepmania_to_chart_jsons("#BPMS:0=120,4=-120;\n#NOTES:dance-single::Hard:3::1000\n;\n", negative);
    CHECK(rejected.size() == 1 && rejected[0].chart.is_null());
    CHECK(rejected[0].error == "negative or zero BPMs are not supported");
}
