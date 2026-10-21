#include "Accuracy.h"
#include "Attempt.h"
#include "Chart.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "DifficultyCalculator.h"
#include "DifficultyModel.h"
#include "OsuFormat.h"
#include "Replay.h"
#include "Results.h"
#include "StepManiaFormat.h"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
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

    Simulation simulation_from(const Attempt& attempt) {
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

    Simulation simulate(const LoadedChart& loaded, const LoadedReplay& loaded_replay) {
        const Replay& replay = loaded_replay.replay;
        check_rate(replay.rate);

        if (replay.chart.key_count != loaded.chart->info.key_count) {
            throw py::value_error("replay key_count does not match the chart");
        }

        Chart pitched = loaded.at_rate(replay.rate);
        Attempt attempt = simulate_replay(pitched, replay);
        return simulation_from(attempt);
    }

    struct LiveAttempt {
        std::unique_ptr<Chart> chart;
        std::unique_ptr<Attempt> attempt;
        std::string hash;
        float rate = 1.0f;
    };

    std::unique_ptr<LiveAttempt> attempt_for(const LoadedChart& loaded, float rate) {
        check_rate(rate);
        auto live = std::make_unique<LiveAttempt>();
        live->chart = std::make_unique<Chart>(loaded.at_rate(rate));
        live->attempt = std::make_unique<Attempt>(live->chart.get());
        live->hash = loaded.hash;
        live->rate = rate;
        return live;
    }

    void check_input(const LiveAttempt& live, uint8_t lane, int32_t time) {
        if (lane >= live.chart->info.key_count) {
            throw py::value_error("lane is outside the chart's key count");
        }

        if (live.attempt->latest_time && time < *live.attempt->latest_time) {
            throw py::value_error("inputs must be given in time order");
        }
    }

    std::unique_ptr<LoadedReplay> attempt_replay(const LiveAttempt& live, const std::string& played_at) {
        double difficulty = DifficultyCalculator::calculate(*live.chart);
        AttemptSummary summary = summarize_attempt(*live.attempt, difficulty);
        ReplayChart chart;
        chart.gameplay_hash = live.hash;
        chart.title = live.chart->info.title;
        chart.artist = live.chart->info.artist;
        chart.difficulty_name = live.chart->info.difficulty_name;
        chart.key_count = live.chart->info.key_count;
        auto loaded = std::make_unique<LoadedReplay>();
        loaded->replay = make_replay(*live.attempt, summary, chart, live.rate, ReplaySettings{}, played_at.empty() ? current_utc_timestamp() : played_at);
        return loaded;
    }

    Hit make_note(uint8_t lane, int32_t time, std::optional<int32_t> end_time) {
        Hit hit;
        hit.key = lane;
        hit.time = time;
        bool hold = end_time.has_value() && *end_time > time;
        hit.hit_type = hold ? HitType::HOLD : HitType::HIT;
        hit.end_time = hold ? *end_time : time;
        return hit;
    }

    int32_t note_end(const Hit& hit) {
        return hit.hit_type == HitType::HOLD ? hit.end_time : hit.time;
    }

    std::string note_repr(const Hit& hit) {
        if (hit.hit_type == HitType::HOLD) {
            return std::format("Note(lane={}, time={}, end_time={})", hit.key, hit.time, hit.end_time);
        }

        return std::format("Note(lane={}, time={})", hit.key, hit.time);
    }

    ChartTimingPoint make_timing_point(int32_t time, double bpm, uint8_t meter) {
        if (!(bpm > 0) || !std::isfinite(bpm)) {
            throw py::value_error("bpm must be positive");
        }

        if (meter == 0) {
            throw py::value_error("meter must be positive");
        }

        ChartTimingPoint point;
        point.time = time;
        point.beat_length = (float)(60000.0 / bpm);
        point.meter = meter;
        return point;
    }

    std::unique_ptr<LoadedChart> chart_create(std::vector<Hit> notes, uint8_t key_count, std::vector<ChartTimingPoint> timing_points, const std::string& title, const std::string& artist, const std::string& mapper, const std::string& difficulty_name, const std::string& audio_file, const std::string& image_file, int32_t preview_time, uint8_t hit_accuracy, uint8_t damage) {
        ChartInfo info;
        info.title = title;
        info.title_unicode = title;
        info.artist = artist;
        info.artist_unicode = artist;
        info.mapper = mapper;
        info.difficulty_name = difficulty_name;
        info.audio_file = audio_file;
        info.image_file = image_file;
        info.preview_time = preview_time;
        info.hit_accuracy = hit_accuracy;
        info.key_count = key_count;
        info.damage = damage;
        std::string error;
        std::unique_ptr<Chart> chart = Chart::create(info, std::move(notes), std::move(timing_points), error);

        if (chart == nullptr) {
            throw py::value_error(error);
        }

        auto loaded = std::make_unique<LoadedChart>();
        loaded->hash = chart->gameplay_hash();
        loaded->chart = std::move(chart);
        return loaded;
    }

    void check_hit_accuracy(uint8_t hit_accuracy) {
        if (hit_accuracy > MAX_HIT_ACC_DIFFICULTY) {
            throw py::value_error(std::format("hit_accuracy must be between {} and {}", MIN_HIT_ACC_DIFFICULTY, MAX_HIT_ACC_DIFFICULTY));
        }
    }

    std::optional<std::string> judge(int32_t error, bool release, uint8_t hit_accuracy) {
        check_hit_accuracy(hit_accuracy);
        HitScore score = get_score_from_hit_err((uint32_t)std::min<int64_t>(std::llabs((int64_t)error), UINT32_MAX), hit_accuracy, release);

        if (score == HitScore::NONE) {
            return std::nullopt;
        }

        return std::string(hit_score_name(score));
    }

    py::dict judgement_windows(uint8_t hit_accuracy, bool release) {
        check_hit_accuracy(hit_accuracy);
        py::dict windows;

        for (HitScore score : TIMED_HIT_SCORES) {
            windows[py::str(std::string(hit_score_name(score)))] = get_window_from_hit_acc_diff(score, hit_accuracy, release);
        }

        return windows;
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

#ifndef NASCENT_PYTHON_MODULE
#define NASCENT_PYTHON_MODULE nascent_core
#endif

#define NASCENT_DEFINE_MODULE(name, variable) PYBIND11_MODULE(name, variable)

NASCENT_DEFINE_MODULE(NASCENT_PYTHON_MODULE, m) {
    m.doc() = "Nascent chart, judge and difficulty code";

    m.attr("CALCULATOR_NAME") = std::string(difficulty_model::NAME);

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

    py::dict hit_score_accuracy;

    for (HitScore score : ALL_HIT_SCORES) {
        if (score != HitScore::NONE) {
            hit_score_accuracy[py::str(std::string(hit_score_name(score)))] = hit_score_acc(score) / 100.0;
        }
    }

    m.attr("HIT_SCORE_ACCURACY") = hit_score_accuracy;
    m.attr("RANKED_HIT_ACCURACY") = RANKED_HIT_ACC_DIFFICULTY;
    m.attr("MAX_HIT_ACCURACY") = MAX_HIT_ACC_DIFFICULTY;

    py::class_<Hit>(m, "Note")
        .def(py::init(&make_note), py::arg("lane"), py::arg("time"), py::arg("end_time") = py::none())
        .def_property_readonly("lane", [](const Hit& hit) { return hit.key; })
        .def_property_readonly("time", [](const Hit& hit) { return hit.time; })
        .def_property_readonly("end_time", &note_end)
        .def_property_readonly("is_hold", [](const Hit& hit) { return hit.hit_type == HitType::HOLD; })
        .def("__eq__", [](const Hit& a, const Hit& b) { return a.key == b.key && a.time == b.time && a.hit_type == b.hit_type && note_end(a) == note_end(b); })
        .def("__repr__", &note_repr);

    py::class_<ChartTimingPoint>(m, "TimingPoint")
        .def(py::init(&make_timing_point), py::arg("time"), py::arg("bpm"), py::arg("meter") = (uint8_t)4)
        .def_property_readonly("time", [](const ChartTimingPoint& point) { return point.time; })
        .def_property_readonly("bpm", [](const ChartTimingPoint& point) { return 60000.0 / point.beat_length; })
        .def_property_readonly("beat_length", [](const ChartTimingPoint& point) { return point.beat_length; })
        .def_property_readonly("meter", [](const ChartTimingPoint& point) { return point.meter; })
        .def("__repr__", [](const ChartTimingPoint& point) { return std::format("TimingPoint(time={}, bpm={:g}, meter={})", point.time, 60000.0 / point.beat_length, point.meter); });

    py::class_<LoadedChart>(m, "Chart")
        .def_static("from_json", &chart_from_json, py::arg("text"))
        .def_static(
            "create",
            &chart_create,
            py::arg("notes"),
            py::arg("key_count"),
            py::arg("timing_points") = std::vector<ChartTimingPoint>{},
            py::arg("title") = "",
            py::arg("artist") = "",
            py::arg("mapper") = "",
            py::arg("difficulty_name") = "",
            py::arg("audio_file") = "",
            py::arg("image_file") = "",
            py::arg("preview_time") = 0,
            py::arg("hit_accuracy") = RANKED_HIT_ACC_DIFFICULTY,
            py::arg("damage") = (uint8_t)5
        )
        .def_property_readonly("notes", [](const LoadedChart& c) { return c.chart->unpitched_hit_objects(); })
        .def("notes_at", [](const LoadedChart& c, float rate) { check_rate(rate); return c.at_rate(rate).hits; }, py::arg("rate"))
        .def_property_readonly("timing_points", [](const LoadedChart& c) { return c.chart->unpitched_timing(); })
        .def("timing_points_at", [](const LoadedChart& c, float rate) { check_rate(rate); return c.at_rate(rate).timing_points; }, py::arg("rate"))
        .def_property_readonly("bpm", [](const LoadedChart& c) { return c.chart->bpm_range().main; })
        .def_property_readonly("bpm_min", [](const LoadedChart& c) { return c.chart->bpm_range().min; })
        .def_property_readonly("bpm_max", [](const LoadedChart& c) { return c.chart->bpm_range().max; })
        .def_property_readonly("audio_file", [](const LoadedChart& c) { return c.chart->info.audio_file; })
        .def_property_readonly("image_file", [](const LoadedChart& c) { return c.chart->info.image_file; })
        .def_property_readonly("preview_time", [](const LoadedChart& c) { return c.chart->info.preview_time; })
        .def_property_readonly("audio_lead_in", [](const LoadedChart& c) { return c.chart->info.audio_lead_in; })
        .def("__repr__", [](const LoadedChart& c) { return std::format("Chart({} - {} [{}], {}K)", c.chart->info.artist, c.chart->info.title, c.chart->info.difficulty_name, c.chart->info.key_count); })
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

    py::class_<LiveAttempt>(m, "Attempt")
        .def(py::init(&attempt_for), py::arg("chart"), py::arg("rate") = 1.0f)
        .def(
            "press",
            [](LiveAttempt& live, uint8_t lane, int32_t time) {
                check_input(live, lane, time);
                live.attempt->press(lane, time);
            },
            py::arg("lane"),
            py::arg("time")
        )
        .def(
            "release",
            [](LiveAttempt& live, uint8_t lane, int32_t time) {
                check_input(live, lane, time);
                live.attempt->release(lane, time);
            },
            py::arg("lane"),
            py::arg("time")
        )
        .def(
            "update",
            [](LiveAttempt& live, int32_t time) {
                check_input(live, 0, time);
                live.attempt->update(time);
            },
            py::arg("time")
        )
        .def_property_readonly("rate", [](const LiveAttempt& live) { return live.rate; })
        .def_property_readonly("accuracy", [](const LiveAttempt& live) { return live.attempt->get_accuracy(); })
        .def_property_readonly("combo", [](const LiveAttempt& live) { return live.attempt->get_combo(); })
        .def_property_readonly("max_combo", [](const LiveAttempt& live) { return live.attempt->max_combo; })
        .def_property_readonly("average_error", [](const LiveAttempt& live) { return live.attempt->get_avg_err(); })
        .def_property_readonly("error_deviation", [](const LiveAttempt& live) { return live.attempt->get_err_deviation(); })
        .def("result", [](const LiveAttempt& live) { return simulation_from(*live.attempt); })
        .def("replay", &attempt_replay, py::arg("played_at") = "");

    m.def("simulate", &simulate, py::arg("chart"), py::arg("replay"));
    m.def("judge", &judge, py::arg("error"), py::arg("release") = false, py::arg("hit_accuracy") = RANKED_HIT_ACC_DIFFICULTY);
    m.def("judgement_windows", &judgement_windows, py::arg("hit_accuracy") = RANKED_HIT_ACC_DIFFICULTY, py::arg("release") = false);
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
    m.def(
        "convert_stepmania",
        [](const std::string& text) {
            std::string error;
            std::vector<StepManiaChart> charts = stepmania_to_chart_jsons(text, error);

            if (charts.empty()) {
                throw py::value_error(error);
            }

            py::list result;

            for (const StepManiaChart& chart : charts) {
                py::dict entry;
                entry["index"] = chart.index;
                entry["steps_type"] = chart.steps_type;
                entry["difficulty"] = chart.difficulty;
                entry["description"] = chart.description;
                entry["meter"] = chart.meter;
                entry["chart"] = chart.chart.is_null() ? py::object(py::none()) : py::object(py::str(chart.chart.dump()));
                entry["error"] = chart.error.empty() ? py::object(py::none()) : py::object(py::str(chart.error));
                result.append(entry);
            }

            return result;
        },
        py::arg("text")
    );
}
