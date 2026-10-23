#pragma once

#include "Replay.h"
#include "ServerClient.h"

#include <cstdint>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nascent {
    class Chart;

    template <typename T>
    struct ApiResult {
        std::optional<T> value;
        ServerResponse response;

        bool ok() const {
            return value.has_value();
        }

        std::string error_code() const {
            return response.error_code();
        }

        std::string error() const {
            return response.ok() ? std::string("the server sent an unexpected response") : response.message();
        }
    };

    struct ServerHealth {
        bool healthy = false;
        bool database = false;
        bool cache = false;
        double latency_ms = 0;
    };

    struct ServerInfo {
        std::vector<float> rates;
        uint32_t replay_format_version = 0;
        std::string chart_hash_format;
        std::string calculator_version;
        std::vector<uint8_t> ranked_key_counts;
        std::string min_client_version;
    };

    struct UserSummary {
        int64_t id = 0;
        std::string username;
    };

    struct AccountUser {
        int64_t id = 0;
        std::string username;
        std::string email;
        std::string role;
        bool restricted = false;
    };

    struct PlayerStats {
        int64_t play_count = 0;
        int64_t ranked_play_count = 0;
        int64_t total_hits = 0;
        int64_t ranked_playtime_ms = 0;
        int64_t total_playtime_ms = 0;
        int64_t max_combo = 0;
        std::map<std::string, int64_t> grade_counts;
    };

    struct KeyModeStats {
        uint8_t key_count = 0;
        std::optional<int64_t> global_rank;
        double performance = 0;
        double accuracy = 0;
        PlayerStats stats;
    };

    struct Profile {
        UserSummary user;
        std::string role;
        bool restricted = false;
        PlayerStats stats;
        std::vector<KeyModeStats> key_modes;
    };

    struct Rank {
        std::optional<int64_t> global_rank;
        double performance = 0;
        int64_t ranked_players = 0;
    };

    struct ServerChartSource {
        std::string source;
        std::string checksum;
        std::optional<int64_t> id;
        std::optional<int64_t> set_id;
        std::string url;
    };

    struct ServerChart {
        std::string hash;
        std::string status;
        std::string title;
        std::string artist;
        std::string mapper;
        std::string difficulty_name;
        uint8_t key_count = 0;
        uint8_t hit_accuracy = 0;
        double difficulty = 0;
        int64_t duration_ms = 0;
        int64_t note_count = 0;
        int64_t hold_count = 0;
        int64_t play_count = 0;
        std::vector<std::pair<float, double>> difficulties;
        std::vector<ServerChartSource> sources;
    };

    struct ServerPlay {
        int64_t id = 0;
        float rate = 1.0f;
        uint8_t key_count = 0;
        std::string status;
        bool ranked = false;
        double accuracy = 0;
        std::string grade;
        int64_t max_combo = 0;
        std::map<std::string, int64_t> score_counts;
        double average_error = 0;
        double difficulty = 0;
        double performance = 0;
        int64_t duration_ms = 0;
        std::string submitted_at;
        bool is_chart_best = false;
        bool is_profile_best = false;
        std::optional<UserSummary> user;
        std::optional<ServerChart> chart;
        std::vector<std::string> flags;
    };

    struct RankedPlay {
        int64_t rank = 0;
        ServerPlay play;
    };

    struct BestPlay {
        int64_t position = 0;
        double weight = 0;
        double weighted_performance = 0;
        ServerPlay play;
    };

    struct RecentPlays {
        std::vector<ServerPlay> plays;
        std::optional<int64_t> next_before;
    };

    struct ChartLeaderboard {
        ServerChart chart;
        float rate = 1.0f;
        int64_t total = 0;
        std::vector<RankedPlay> entries;
        std::optional<RankedPlay> viewer;
    };

    struct LeaderboardEntry {
        int64_t rank = 0;
        UserSummary user;
        double performance = 0;
        double accuracy = 0;
        int64_t ranked_play_count = 0;
    };

    struct Leaderboard {
        std::optional<uint8_t> key_count;
        int64_t total = 0;
        std::vector<LeaderboardEntry> entries;
    };

    struct ChartQuery {
        std::string search;
        std::string status;
        std::optional<uint8_t> key_count;
        std::optional<double> min_difficulty;
        std::optional<double> max_difficulty;
        std::optional<double> min_duration_s;
        std::optional<double> max_duration_s;
        std::optional<double> min_notes;
        std::optional<double> max_notes;
        std::string source;
        bool downloadable = false;
        std::string sort = "difficulty";
        bool descending = false;
        int32_t limit = 50;
        int32_t offset = 0;

        bool operator==(const ChartQuery&) const = default;
    };

    struct ChartList {
        int64_t total = 0;
        std::vector<ServerChart> charts;
    };

    struct PlayTicket {
        std::string ticket;
        std::string chart_hash;
        float rate = 1.0f;
        std::string expires_at;
        bool chart_known = false;
        std::string chart_status;
    };

    struct Standing {
        uint8_t key_count = 0;
        double performance = 0;
        double performance_change = 0;
        std::optional<int64_t> global_rank;
        std::optional<int64_t> previous_global_rank;
    };

    struct Submission {
        ServerPlay play;
        ServerChart chart;
        bool chart_created = false;
        std::optional<int64_t> chart_rank;
        std::optional<Standing> key_mode;
        std::optional<double> previous_best_performance;
    };

    class ServerApi {
        public:
        static std::string website_url(const std::string& route);
        static std::string chart_page_url(const std::string& chart_hash, float rate = 1.0f);
        static std::string profile_page_url(const std::string& user);
        static std::string play_page_url(int64_t play_id);

        static std::future<ApiResult<ServerHealth>> health();
        static std::future<ApiResult<ServerHealth>> health(const std::string& server_url);
        static std::future<ApiResult<ServerInfo>> info();

        static std::future<ApiResult<AccountUser>> me();
        static std::future<ApiResult<bool>> change_password(const std::string& current_password, const std::string& new_password);

        static std::future<ApiResult<Profile>> profile(const std::string& user);
        static std::future<ApiResult<Rank>> rank(const std::string& user, uint8_t key_count);
        static std::future<ApiResult<std::vector<BestPlay>>> best_plays(const std::string& user, uint8_t key_count, int32_t limit = 50, int32_t offset = 0);
        static std::future<ApiResult<RecentPlays>> recent_plays(const std::string& user, int32_t limit = 50, std::optional<int64_t> before = std::nullopt);
        static std::future<ApiResult<std::vector<ServerPlay>>> chart_plays(const std::string& user, const std::string& chart_hash, std::optional<float> rate = std::nullopt);

        static std::future<ApiResult<ServerChart>> chart(const std::string& chart_hash);
        static std::future<ApiResult<ChartList>> charts(const ChartQuery& query);
        static std::future<ApiResult<ChartLeaderboard>> chart_leaderboard(const std::string& chart_hash, float rate = 1.0f, int32_t limit = 50, int32_t offset = 0);
        static std::future<ApiResult<ChartLeaderboard>> chart_performance_leaderboard(const std::string& chart_hash, int32_t limit = 50, int32_t offset = 0);
        static std::future<ApiResult<ServerChart>> upload_chart(const Chart& chart);

        static std::future<ApiResult<Leaderboard>> leaderboard(std::optional<uint8_t> key_count = std::nullopt, int32_t limit = 50, int32_t offset = 0);

        static std::future<ApiResult<PlayTicket>> request_ticket(const Chart& chart);
        static std::future<ApiResult<Submission>> submit_play(const Chart& chart, const Replay& replay, const std::string& ticket = "");
        static ApiResult<Submission> send_submission(nlohmann::json body, const nlohmann::json& chart_data);
        static bool is_retryable(const ServerResponse& response);
        static std::future<ApiResult<Submission>> submit_play(const Chart& chart, const Replay& replay, std::shared_future<ApiResult<PlayTicket>> ticket);
        static std::future<ApiResult<ServerPlay>> play(int64_t play_id);
        static std::future<ApiResult<Replay>> replay(int64_t play_id);
    };
}
