#include "Replay.h"
#include "Utf8Path.h"

#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <system_error>

namespace nascent {
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
                          {"sha256", replay.chart.sha256},
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
    }

    void from_json(const nlohmann::json& j, Replay& replay) {
        j.at("version").get_to(replay.version);
        j.at("played_at").get_to(replay.played_at);

        const nlohmann::json& chart = j.at("chart");
        chart.at("path").get_to(replay.chart.path);
        chart.at("sha256").get_to(replay.chart.sha256);
        chart.at("title").get_to(replay.chart.title);
        chart.at("artist").get_to(replay.chart.artist);
        chart.at("difficulty_name").get_to(replay.chart.difficulty_name);
        chart.at("key_count").get_to(replay.chart.key_count);

        j.at("rate").get_to(replay.rate);

        const nlohmann::json& settings = j.at("settings");
        settings.at("audio_input_offset").get_to(replay.settings.audio_input_offset);
        settings.at("audio_visual_offset").get_to(replay.settings.audio_visual_offset);
        settings.at("scroll_speed").get_to(replay.settings.scroll_speed);

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

    std::optional<std::filesystem::path> save_replay(const Replay& replay, const std::filesystem::path& directory, std::string& error) {
        std::error_code ec;
        std::filesystem::path chart_directory = directory / (replay.chart.sha256.empty() ? std::string("unknown") : replay.chart.sha256.substr(0, Replay::FOLDER_HASH_LENGTH));
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
