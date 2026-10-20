#include "Accuracy.h"
#include "Attempt.h"
#include "Chart.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "DifficultyCalculator.h"
#include "OsuFormat.h"
#include "Replay.h"
#include "Results.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace py = pybind11;
using namespace nascent;

namespace {
    struct LoadedChart {
        std::unique_ptr<Chart> chart;
        std::string hash;

        Chart at_rate(float rate) const {
            Chart pitched = *chart;
            pitched.set_pitch(rate);
            return pitched;
        }
    };

    struct LoadedReplay {
        Replay replay;
    };

    struct JudgedNote {
        uint8_t lane = 0;
        bool is_hold = false;
        int32_t time = 0;
        int32_t end_time = 0;
        bool hit_played = false;
        int32_t hit_error = 0;
        std::string hit_score;
        bool release_played = false;
        int32_t release_error = 0;
        std::string release_score;
    };

    struct Simulation {
        double accuracy = 0.0;
        std::string grade;
        uint32_t max_combo = 0;
        std::map<std::string, uint32_t> score_counts;
        double average_error = 0.0;
        std::vector<JudgedNote> notes;
    };

    void check_rate(float rate) {
        if (!(rate > 0.0f)) {
            throw py::value_error("rate must be positive");
        }
    }

    std::unique_ptr<LoadedChart> chart_from_json(const std::string& text) {
        nlohmann::json data;

        try {
            data = nlohmann::json::parse(text);
        }
        catch (const nlohmann::json::exception& e) {
            throw py::value_error(e.what());
        }

        std::string error;
        std::unique_ptr<Chart> chart = Chart::parse(data, 1.0f, error);

        if (chart == nullptr) {
            throw py::value_error(error);
        }

        auto loaded = std::make_unique<LoadedChart>();
        loaded->hash = chart->gameplay_hash();
        loaded->chart = std::move(chart);
        return loaded;
    }

    std::unique_ptr<LoadedReplay> replay_from_json(const std::string& text) {
        auto loaded = std::make_unique<LoadedReplay>();

        try {
            nlohmann::json::parse(text).get_to(loaded->replay);
        }
        catch (const nlohmann::json::exception& e) {
            throw py::value_error(e.what());
        }

        return loaded;
    }

    std::string score_name(HitScore score) {
        return score == HitScore::NONE ? std::string() : std::string(hit_score_name(score));
    }

    Simulation simulate(const LoadedChart& loaded, const LoadedReplay& loaded_replay) {
        const Replay& replay = loaded_replay.replay;
        check_rate(replay.rate);

        if (replay.chart.key_count != loaded.chart->info.key_count) {
            throw py::value_error("replay key_count does not match the chart");
        }

        Chart pitched = loaded.at_rate(replay.rate);
        Attempt attempt = simulate_replay(pitched, replay);
        AttemptSummary summary = summarize_attempt(attempt, 0.0);
        ReplayResult result = replay_result_from(summary);

        Simulation simulation;
        simulation.accuracy = summary.accuracy;
        simulation.grade = result.grade;
        simulation.max_combo = summary.max_combo;
        simulation.score_counts = result.score_counts;
        simulation.average_error = summary.average_error;
        simulation.notes.reserve(attempt.judged_hits.size());

        for (const JudgedHit& jhit : attempt.judged_hits) {
            JudgedNote note;
            note.lane = jhit.chart_hit.key;
            note.is_hold = jhit.chart_hit.hit_type == HitType::HOLD;
            note.time = jhit.chart_hit.time;
            note.end_time = jhit.chart_hit.end_time;
            note.hit_played = jhit.hit_played;
            note.hit_error = jhit.hit_err;
            note.hit_score = score_name(jhit.hit_score);
            note.release_played = jhit.release_played;
            note.release_error = jhit.release_err;
            note.release_score = score_name(jhit.release_score);
            simulation.notes.push_back(std::move(note));
        }

        return simulation;
    }

    py::dict claimed_result(const Replay& replay) {
        py::dict result;
        result["accuracy"] = replay.result.accuracy;
        result["grade"] = replay.result.grade;
        result["max_combo"] = replay.result.max_combo;
        result["score_counts"] = replay.result.score_counts;
        result["average_error"] = replay.result.average_error;
        result["difficulty"] = replay.result.difficulty;
        result["performance"] = replay.result.performance;
        return result;
    }

    std::vector<std::tuple<int32_t, uint8_t, bool>> replay_inputs(const Replay& replay) {
        std::vector<std::tuple<int32_t, uint8_t, bool>> inputs;
        inputs.reserve(replay.inputs.size());

        for (const AttemptInput& input : replay.inputs) {
            inputs.emplace_back(input.time, input.key, input.pressed);
        }

        return inputs;
    }

    int32_t chart_end_at(const LoadedChart& chart, float rate) {
        check_rate(rate);
        return chart.at_rate(rate).get_chart_end_ms();
    }

    int32_t chart_intro_skip_at(const LoadedChart& chart, float rate) {
        check_rate(rate);
        return chart.at_rate(rate).intro_skip_ms();
    }

    int32_t chart_duration_at(const LoadedChart& chart, float rate) {
        check_rate(rate);
        return chart.at_rate(rate).get_duration_ms();
    }

    double chart_difficulty_at(const LoadedChart& chart, float rate) {
        check_rate(rate);
        Chart pitched = chart.at_rate(rate);
        py::gil_scoped_release release;
        return DifficultyCalculator::calculate(pitched);
    }

    size_t chart_hold_count(const LoadedChart& chart) {
        size_t holds = 0;

        for (const Hit& hit : chart.chart->hits) {
            holds += hit.hit_type == HitType::HOLD ? 1 : 0;
        }

        return holds;
    }
}

PYBIND11_MODULE(nascent_core, m) {
    m.doc() = "Nascent chart, judge and difficulty code";

    m.attr("RATE_MIN") = PITCH_MINIMUM;
    m.attr("RATE_MAX") = PITCH_MAXIMUM;
    m.attr("RATE_STEP") = PITCH_INTERVAL;
    m.attr("INTRO_SKIP_MIN_GAP_MS") = INTRO_SKIP_MIN_GAP_MS;
    m.attr("INTRO_SKIP_LEAD_MS") = INTRO_SKIP_LEAD_MS;
    m.attr("REPLAY_FORMAT_VERSION") = Replay::FORMAT_VERSION;
    m.attr("CHART_HASH_FORMAT") = std::string(Chart::CHART_HASH_FORMAT);
    m.attr("PERFORMANCE_ACCURACY_FLOOR") = PERFORMANCE_ACCURACY_FLOOR;

    std::vector<std::string> hit_scores;

    for (HitScore score : ALL_HIT_SCORES) {
        if (score != HitScore::NONE) {
            hit_scores.emplace_back(hit_score_name(score));
        }
    }

    m.attr("HIT_SCORES") = hit_scores;

    py::class_<LoadedChart>(m, "Chart")
        .def_static("from_json", &chart_from_json, py::arg("text"))
        .def_property_readonly("hash", [](const LoadedChart& c) { return c.hash; })
        .def_property_readonly("key_count", [](const LoadedChart& c) { return c.chart->info.key_count; })
        .def_property_readonly("hit_accuracy", [](const LoadedChart& c) { return c.chart->info.hit_accuracy; })
        .def_property_readonly("damage", [](const LoadedChart& c) { return c.chart->info.damage; })
        .def_property_readonly("title", [](const LoadedChart& c) { return c.chart->info.title; })
        .def_property_readonly("title_unicode", [](const LoadedChart& c) { return c.chart->info.title_unicode; })
        .def_property_readonly("artist", [](const LoadedChart& c) { return c.chart->info.artist; })
        .def_property_readonly("artist_unicode", [](const LoadedChart& c) { return c.chart->info.artist_unicode; })
        .def_property_readonly("mapper", [](const LoadedChart& c) { return c.chart->info.mapper; })
        .def_property_readonly("difficulty_name", [](const LoadedChart& c) { return c.chart->info.difficulty_name; })
        .def_property_readonly("note_count", [](const LoadedChart& c) { return c.chart->hits.size(); })
        .def_property_readonly("hold_count", &chart_hold_count)
        .def_property_readonly("start_ms", [](const LoadedChart& c) { return c.chart->get_chart_start_ms(); })
        .def_property_readonly("duration_ms", [](const LoadedChart& c) { return c.chart->get_duration_ms(); })
        .def("end_ms_at", &chart_end_at, py::arg("rate"))
        .def("intro_skip_ms_at", &chart_intro_skip_at, py::arg("rate"))
        .def("duration_ms_at", &chart_duration_at, py::arg("rate"))
        .def("difficulty", &chart_difficulty_at, py::arg("rate") = 1.0f)
        .def("to_json", [](const LoadedChart& c) { return c.chart->serialize().dump(); });

    py::class_<LoadedReplay>(m, "Replay")
        .def_static("from_json", &replay_from_json, py::arg("text"))
        .def_property_readonly("version", [](const LoadedReplay& r) { return r.replay.version; })
        .def_property_readonly("rate", [](const LoadedReplay& r) { return r.replay.rate; })
        .def_property_readonly("end_time", [](const LoadedReplay& r) { return r.replay.end_time; })
        .def_property_readonly("key_count", [](const LoadedReplay& r) { return r.replay.chart.key_count; })
        .def_property_readonly("inputs", [](const LoadedReplay& r) { return replay_inputs(r.replay); })
        .def_property_readonly("settings", [](const LoadedReplay& r) {
            py::dict settings;
            settings["audio_input_offset"] = r.replay.settings.audio_input_offset;
            settings["audio_visual_offset"] = r.replay.settings.audio_visual_offset;
            settings["scroll_speed"] = r.replay.settings.scroll_speed;
            settings["mirror"] = r.replay.settings.mirror;
            return settings;
        })
        .def_property_readonly("claimed_result", [](const LoadedReplay& r) { return claimed_result(r.replay); })
        .def("to_json", [](const LoadedReplay& r) { return nlohmann::json(r.replay).dump(); });

    py::class_<JudgedNote>(m, "JudgedNote")
        .def_readonly("lane", &JudgedNote::lane)
        .def_readonly("is_hold", &JudgedNote::is_hold)
        .def_readonly("time", &JudgedNote::time)
        .def_readonly("end_time", &JudgedNote::end_time)
        .def_readonly("hit_played", &JudgedNote::hit_played)
        .def_readonly("hit_error", &JudgedNote::hit_error)
        .def_readonly("hit_score", &JudgedNote::hit_score)
        .def_readonly("release_played", &JudgedNote::release_played)
        .def_readonly("release_error", &JudgedNote::release_error)
        .def_readonly("release_score", &JudgedNote::release_score);

    py::class_<Simulation>(m, "Simulation")
        .def_readonly("accuracy", &Simulation::accuracy)
        .def_readonly("grade", &Simulation::grade)
        .def_readonly("max_combo", &Simulation::max_combo)
        .def_readonly("score_counts", &Simulation::score_counts)
        .def_readonly("average_error", &Simulation::average_error)
        .def_readonly("notes", &Simulation::notes);

    m.def("simulate", &simulate, py::arg("chart"), py::arg("replay"));
    m.def("performance", &performance_for, py::arg("difficulty"), py::arg("accuracy"));
    m.def("grade", [](double accuracy) { return std::string(grade_name(grade_for_accuracy(accuracy))); }, py::arg("accuracy"));
    m.def("difficulty_fingerprint", []() { return DifficultyCache::fingerprint(); });
    m.def(
        "convert_osu",
        [](const std::string& text) {
            std::string error;
            std::optional<nlohmann::json> chart = osu_to_chart_json(text, error);

            if (!chart) {
                throw py::value_error(error);
            }

            return chart->dump();
        },
        py::arg("text")
    );
}
