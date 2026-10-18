#include "Replay.h"
#include "Utf8Path.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <system_error>

namespace nascent {
    static double rounded(double value, double scale) {
        return std::round(value * scale) / scale;
    }

    void to_json(nlohmann::json& j, const Replay& replay) {
        nlohmann::json inputs = nlohmann::json::array();

        for (const AttemptInput& input : replay.inputs) {
            inputs.push_back({input.time, input.key, input.pressed ? 1 : 0});
        }

        j = nlohmann::json{
            {"version", replay.version},
            {"played_at", replay.played_at},
            {"chart", {
                          {"path", replay.chart.path},
                          {"gameplay_hash", replay.chart.gameplay_hash},
                          {"title", replay.chart.title},
                          {"artist", replay.chart.artist},
                          {"difficulty_name", replay.chart.difficulty_name},
                          {"key_count", replay.chart.key_count},
                      }},
            {"rate", replay.rate},
            {"settings", {
                             {"audio_input_offset", replay.settings.audio_input_offset},
                             {"audio_visual_offset", replay.settings.audio_visual_offset},
                             {"scroll_speed", replay.settings.scroll_speed},
                             {"mirror", replay.settings.mirror},
                         }},
            {"end_time", replay.end_time},
            {"result", {
                           {"accuracy", replay.result.accuracy},
                           {"grade", replay.result.grade},
                           {"max_combo", replay.result.max_combo},
                           {"score_counts", replay.result.score_counts},
                           {"average_error", replay.result.average_error},
                           {"difficulty", replay.result.difficulty},
                           {"performance", replay.result.performance},
                       }},
            {"inputs", inputs},
        };

        if (!replay.diagnostics.is_null()) {
            j["diagnostics"] = replay.diagnostics;
        }

        if (!replay.input_times.empty() && replay.input_times.size() == replay.inputs.size()) {
            nlohmann::json times = nlohmann::json::array();

            for (double time : replay.input_times) {
                times.push_back(rounded(time, 1000.0));
            }

            j["input_times"] = std::move(times);
        }

        if (!replay.clock_samples.empty()) {
            nlohmann::json samples = nlohmann::json::array();

            for (const std::array<double, 2>& sample : replay.clock_samples) {
                samples.push_back({rounded(sample[0], 10.0), rounded(sample[1], 10.0)});
            }

            j["clock_samples"] = std::move(samples);
        }

        if (!replay.pauses.empty()) {
            nlohmann::json pauses = nlohmann::json::array();

            for (const std::array<double, 3>& pause : replay.pauses) {
                pauses.push_back({rounded(pause[0], 10.0), rounded(pause[1], 10.0), rounded(pause[2], 10.0)});
            }

            j["pauses"] = std::move(pauses);
        }
    }

    void from_json(const nlohmann::json& j, Replay& replay) {
        j.at("version").get_to(replay.version);
        j.at("played_at").get_to(replay.played_at);

        const nlohmann::json& chart = j.at("chart");
        chart.at("path").get_to(replay.chart.path);
        replay.chart.gameplay_hash = chart.value("gameplay_hash", std::string());
        chart.at("title").get_to(replay.chart.title);
        chart.at("artist").get_to(replay.chart.artist);
        chart.at("difficulty_name").get_to(replay.chart.difficulty_name);
        chart.at("key_count").get_to(replay.chart.key_count);

        j.at("rate").get_to(replay.rate);

        const nlohmann::json& settings = j.at("settings");
        settings.at("audio_input_offset").get_to(replay.settings.audio_input_offset);
        settings.at("audio_visual_offset").get_to(replay.settings.audio_visual_offset);
        settings.at("scroll_speed").get_to(replay.settings.scroll_speed);
        replay.settings.mirror = settings.value("mirror", false);

        j.at("end_time").get_to(replay.end_time);

        const nlohmann::json& result = j.at("result");
        result.at("accuracy").get_to(replay.result.accuracy);
        result.at("grade").get_to(replay.result.grade);
        result.at("max_combo").get_to(replay.result.max_combo);
        result.at("score_counts").get_to(replay.result.score_counts);
        result.at("average_error").get_to(replay.result.average_error);
        result.at("difficulty").get_to(replay.result.difficulty);
        result.at("performance").get_to(replay.result.performance);

        replay.diagnostics = j.contains("diagnostics") ? j.at("diagnostics") : nlohmann::json();
        replay.inputs.clear();

        for (const nlohmann::json& input : j.at("inputs")) {
            replay.inputs.push_back({input.at(0).get<int32_t>(), input.at(1).get<uint8_t>(), input.at(2).get<int>() != 0});
        }

        replay.input_times.clear();

        if (j.contains("input_times") && j.at("input_times").is_array() && j.at("input_times").size() == replay.inputs.size()) {
            for (const nlohmann::json& time : j.at("input_times")) {
                if (!time.is_number()) {
                    replay.input_times.clear();
                    break;
                }

                replay.input_times.push_back(time.get<double>());
            }
        }

        replay.clock_samples.clear();

        if (j.contains("clock_samples") && j.at("clock_samples").is_array()) {
            for (const nlohmann::json& sample : j.at("clock_samples")) {
                if (sample.is_array() && sample.size() >= 2 && sample.at(0).is_number() && sample.at(1).is_number()) {
                    replay.clock_samples.push_back({sample.at(0).get<double>(), sample.at(1).get<double>()});
                }
            }
        }

        replay.pauses.clear();

        if (j.contains("pauses") && j.at("pauses").is_array()) {
            for (const nlohmann::json& pause : j.at("pauses")) {
                if (pause.is_array() && pause.size() >= 3 && pause.at(0).is_number() && pause.at(1).is_number() && pause.at(2).is_number()) {
                    replay.pauses.push_back({pause.at(0).get<double>(), pause.at(1).get<double>(), pause.at(2).get<double>()});
                }
            }
        }
    }

    std::string current_utc_timestamp() {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm* utc = std::gmtime(&now);

        if (utc == nullptr) {
            return "1970-01-01T00:00:00Z";
        }

        return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z", utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min, utc->tm_sec);
    }

    ReplayResult replay_result_from(const AttemptSummary& summary) {
        ReplayResult result;
        result.accuracy = summary.accuracy;
        result.grade = std::string(grade_name(summary.grade));
        result.max_combo = summary.max_combo;
        result.average_error = summary.average_error;
        result.difficulty = summary.difficulty;
        result.performance = summary.performance;

        for (HitScore score : ALL_HIT_SCORES) {
            if (score != HitScore::NONE) {
                result.score_counts[std::string(hit_score_name(score))] = summary.score_counts[(size_t)score];
            }
        }

        return result;
    }

    Replay make_replay(const Attempt& attempt, const AttemptSummary& summary, const ReplayChart& chart, float rate, const ReplaySettings& settings, const std::string& played_at) {
        Replay replay;
        replay.played_at = played_at;
        replay.chart = chart;
        replay.rate = rate;
        replay.settings = settings;
        replay.inputs = attempt.inputs;
        replay.end_time = attempt.latest_time.value_or(0);
        replay.result = replay_result_from(summary);
        return replay;
    }

    Attempt simulate_replay(const Chart& chart, const Replay& replay) {
        Attempt attempt(&chart);

        for (const AttemptInput& input : replay.inputs) {
            if (input.pressed) {
                attempt.press(input.key, input.time);
            }
            else {
                attempt.release(input.key, input.time);
            }
        }

        attempt.update(replay.end_time);
        return attempt;
    }

    std::filesystem::path replay_folder(const std::filesystem::path& directory, const std::string& gameplay_hash) {
        return directory / (gameplay_hash.empty() ? std::string("unknown") : gameplay_hash.substr(0, Replay::FOLDER_HASH_LENGTH));
    }

    std::optional<std::filesystem::path> save_replay(const Replay& replay, const std::filesystem::path& directory, std::string& error) {
        std::error_code ec;
        std::filesystem::path chart_directory = replay_folder(directory, replay.chart.gameplay_hash);
        std::filesystem::create_directories(chart_directory, ec);

        if (ec) {
            error = std::format("could not create {}: {}", path_utf8(chart_directory), ec.message());
            return std::nullopt;
        }

        std::string stem;

        for (char c : replay.played_at) {
            if (c != '-' && c != ':') {
                stem.push_back(c);
            }
        }

        std::filesystem::path path = chart_directory / (stem + ".json");

        for (int suffix = 1; std::filesystem::exists(path, ec); suffix++) {
            path = chart_directory / std::format("{}-{}.json", stem, suffix);
        }

        std::ofstream ofs(path, std::ios::trunc);

        if (!ofs) {
            error = std::format("could not write {}", path_utf8(path));
            return std::nullopt;
        }

        ofs << nlohmann::json(replay).dump() << std::endl;

        if (!ofs) {
            error = std::format("could not write {}", path_utf8(path));
            return std::nullopt;
        }

        return path;
    }

    std::optional<Replay> load_replay(const std::filesystem::path& path, std::string& error) {
        std::ifstream ifs(path);

        if (!ifs) {
            error = std::format("could not open {}", path_utf8(path));
            return std::nullopt;
        }

        try {
            return nlohmann::json::parse(ifs).get<Replay>();
        }
        catch (const nlohmann::json::exception& e) {
            error = e.what();
            return std::nullopt;
        }
    }
}
