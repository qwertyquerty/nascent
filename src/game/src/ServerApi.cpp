#include "ServerApi.h"

#include "Account.h"
#include "Chart.h"
#include "SettingsManager.h"

#include <format>

namespace nascent {
    using nlohmann::json;

    namespace {
        enum class Auth {
            PUBLIC,
            VIEWER,
            SIGNED_IN
        };

        const json& field(const json& object, const char* key) {
            static const json missing;

            if (!object.is_object()) {
                return missing;
            }

            auto it = object.find(key);
            return it != object.end() ? *it : missing;
        }

        int64_t get_int(const json& object, const char* key, int64_t fallback = 0) {
            const json& value = field(object, key);
            return value.is_number() ? value.get<int64_t>() : fallback;
        }

        std::optional<int64_t> get_optional_int(const json& object, const char* key) {
            const json& value = field(object, key);
            return value.is_number() ? std::optional<int64_t>(value.get<int64_t>()) : std::nullopt;
        }

        double get_double(const json& object, const char* key) {
            const json& value = field(object, key);
            return value.is_number() ? value.get<double>() : 0.0;
        }

        std::string get_string(const json& object, const char* key) {
            const json& value = field(object, key);
            return value.is_string() ? value.get<std::string>() : std::string();
        }

        bool get_bool(const json& object, const char* key) {
            const json& value = field(object, key);
            return value.is_boolean() && value.get<bool>();
        }

        std::map<std::string, int64_t> get_counts(const json& object, const char* key) {
            std::map<std::string, int64_t> counts;

            for (const auto& [name, value] : field(object, key).items()) {
                if (value.is_number()) {
                    counts[name] = value.get<int64_t>();
                }
            }

            return counts;
        }

        template <typename T, typename Parse>
        std::vector<T> get_list(const json& object, const char* key, Parse parse) {
            std::vector<T> items;
            const json& list = field(object, key);

            if (list.is_array()) {
                for (const json& item : list) {
                    items.push_back(parse(item));
                }
            }

            return items;
        }

        UserSummary parse_user(const json& j) {
            return {get_int(j, "id"), get_string(j, "username")};
        }

        PlayerStats parse_stats(const json& j) {
            PlayerStats stats;
            stats.play_count = get_int(j, "play_count");
            stats.ranked_play_count = get_int(j, "ranked_play_count");
            stats.total_hits = get_int(j, "total_hits");
            stats.ranked_playtime_ms = get_int(j, "ranked_playtime_ms");
            stats.total_playtime_ms = get_int(j, "total_playtime_ms");
            stats.max_combo = get_int(j, "max_combo");
            stats.grade_counts = get_counts(j, "grade_counts");
            return stats;
        }

        ServerChart parse_chart(const json& j) {
            ServerChart chart;
            chart.hash = get_string(j, "hash");
            chart.status = get_string(j, "status");
            chart.title = get_string(j, "title");
            chart.artist = get_string(j, "artist");
            chart.mapper = get_string(j, "mapper");
            chart.difficulty_name = get_string(j, "difficulty_name");
            chart.key_count = (uint8_t)get_int(j, "key_count");
            chart.hit_accuracy = (uint8_t)get_int(j, "hit_accuracy");
            chart.difficulty = get_double(j, "difficulty");
            chart.duration_ms = get_int(j, "duration_ms");
            chart.note_count = get_int(j, "note_count");
            chart.hold_count = get_int(j, "hold_count");
            chart.play_count = get_int(j, "play_count");
            chart.difficulties = get_list<std::pair<float, double>>(j, "difficulties", [](const json& entry) {
                return std::pair<float, double>((float)get_double(entry, "rate"), get_double(entry, "difficulty"));
            });
            chart.sources = get_list<ServerChartSource>(j, "sources", [](const json& entry) {
                return ServerChartSource{get_string(entry, "source"), get_string(entry, "checksum"), get_optional_int(entry, "id"), get_optional_int(entry, "set_id"), get_string(entry, "url")};
            });
            return chart;
        }

        ServerPlay parse_play(const json& j) {
            ServerPlay play;
            play.id = get_int(j, "id");
            play.rate = (float)get_double(j, "rate");
            play.key_count = (uint8_t)get_int(j, "key_count");
            play.status = get_string(j, "status");
            play.ranked = get_bool(j, "ranked");
            play.accuracy = get_double(j, "accuracy");
            play.grade = get_string(j, "grade");
            play.max_combo = get_int(j, "max_combo");
            play.score_counts = get_counts(j, "score_counts");
            play.average_error = get_double(j, "average_error");
            play.difficulty = get_double(j, "difficulty");
            play.performance = get_double(j, "performance");
            play.duration_ms = get_int(j, "duration_ms");
            play.submitted_at = get_string(j, "submitted_at");
            play.is_chart_best = get_bool(j, "is_chart_best");
            play.is_profile_best = get_bool(j, "is_profile_best");

            if (field(j, "user").is_object()) {
                play.user = parse_user(j["user"]);
            }

            if (field(j, "chart").is_object()) {
                play.chart = parse_chart(j["chart"]);
            }

            play.flags = get_list<std::string>(j, "flags", [](const json& flag) {
                return get_string(flag, "code");
            });
            return play;
        }

        RankedPlay parse_ranked_play(const json& j) {
            return {get_int(j, "rank"), parse_play(field(j, "play"))};
        }

        Standing parse_standing(const json& j) {
            Standing standing;
            standing.key_count = (uint8_t)get_int(j, "key_count");
            standing.performance = get_double(j, "performance");
            standing.performance_change = get_double(j, "performance_change");
            standing.global_rank = get_optional_int(j, "global_rank");
            standing.previous_global_rank = get_optional_int(j, "previous_global_rank");
            return standing;
        }

        ServerResponse send(Auth auth, const std::string& server_url, const std::string& method, const std::string& path, const json& body) {
            if (auth == Auth::SIGNED_IN || (auth == Auth::VIEWER && Account::signed_in())) {
                return Account::authorized(method, path, body);
            }

            return ServerClient::request(server_url, method, path, body);
        }

        template <typename T, typename Parse>
        std::future<ApiResult<T>> call(Auth auth, std::string method, std::string path, json body, Parse parse) {
            std::string server_url = SettingsManager::settings.server_url;

            return run_detached([auth, server_url, method, path, body, parse] {
                ApiResult<T> result;
                result.response = send(auth, server_url, method, path, body);

                if (result.response.ok()) {
                    try {
                        result.value = parse(result.response.body);
                    }
                    catch (const json::exception&) {
                        result.value.reset();
                    }
                }

                return result;
            });
        }

        std::string user_path(const std::string& user) {
            return "/users/" + ServerClient::url_encode(user);
        }

        std::string key_count_query(std::optional<uint8_t> key_count, char separator) {
            return key_count ? std::format("{}key_count={}", separator, *key_count) : std::string();
        }
    }

    std::string ServerApi::website_url(const std::string& route) {
        std::string base = SettingsManager::settings.server_url;

        while (base.ends_with('/')) {
            base.pop_back();
        }

        return std::format("{}/#{}", base, route);
    }

    std::string ServerApi::chart_page_url(const std::string& chart_hash, float rate) {
        std::string route = "/charts/" + ServerClient::url_encode(chart_hash);

        if (std::abs(rate - 1.0f) > 0.001f) {
            route += std::format("?rate={:g}", rate);
        }

        return website_url(route);
    }

    std::string ServerApi::profile_page_url(const std::string& user) {
        return website_url(user_path(user));
    }

    std::future<ApiResult<ServerHealth>> ServerApi::health() {
        std::string server_url = SettingsManager::settings.server_url;

        return run_detached([server_url] {
            ApiResult<ServerHealth> result;
            result.response = ServerClient::request(server_url, "GET", "/health");

            if (result.response.error.empty() && result.response.body.is_object() && result.response.body.contains("status")) {
                const json& body = result.response.body;
                result.value = ServerHealth{get_string(body, "status") == "ok", get_bool(body, "database"), get_bool(body, "cache"), result.response.latency_ms};
            }

            return result;
        });
    }

    std::future<ApiResult<ServerInfo>> ServerApi::info() {
        return call<ServerInfo>(Auth::PUBLIC, "GET", "/info", nullptr, [](const json& j) {
            ServerInfo info;
            info.rates = get_list<float>(j, "rates", [](const json& rate) { return rate.get<float>(); });
            info.replay_format_version = (uint32_t)get_int(j, "replay_format_version");
            info.chart_hash_format = get_string(j, "chart_hash_format");
            info.calculator_version = get_string(j, "calculator_version");
            info.ranked_key_counts = get_list<uint8_t>(j, "ranked_key_counts", [](const json& count) { return count.get<uint8_t>(); });
            return info;
        });
    }

    std::future<ApiResult<AccountUser>> ServerApi::me() {
        return call<AccountUser>(Auth::SIGNED_IN, "GET", "/me", nullptr, [](const json& j) {
            const json& user = field(j, "user");
            return AccountUser{get_int(user, "id"), get_string(user, "username"), get_string(user, "email"), get_string(user, "role"), get_bool(user, "is_restricted")};
        });
    }

    std::future<ApiResult<bool>> ServerApi::change_password(const std::string& current_password, const std::string& new_password) {
        return call<bool>(Auth::SIGNED_IN, "PUT", "/me/password", {{"current_password", current_password}, {"new_password", new_password}}, [](const json&) { return true; });
    }

    std::future<ApiResult<Profile>> ServerApi::profile(const std::string& user) {
        return call<Profile>(Auth::VIEWER, "GET", user_path(user), nullptr, [](const json& j) {
            const json& user = field(j, "user");
            Profile profile;
            profile.user = parse_user(user);
            profile.role = get_string(user, "role");
            profile.restricted = get_bool(user, "is_restricted");
            profile.stats = parse_stats(field(user, "stats"));
            profile.key_modes = get_list<KeyModeStats>(user, "key_modes", [](const json& mode) {
                return KeyModeStats{(uint8_t)get_int(mode, "key_count"), get_optional_int(mode, "global_rank"), get_double(mode, "performance"), get_double(mode, "accuracy"), parse_stats(mode)};
            });
            return profile;
        });
    }

    std::future<ApiResult<Rank>> ServerApi::rank(const std::string& user, uint8_t key_count) {
        return call<Rank>(Auth::PUBLIC, "GET", user_path(user) + "/rank" + key_count_query(key_count, '?'), nullptr, [](const json& j) {
            return Rank{get_optional_int(j, "global_rank"), get_double(j, "performance"), get_int(j, "ranked_players")};
        });
    }

    std::future<ApiResult<std::vector<BestPlay>>> ServerApi::best_plays(const std::string& user, uint8_t key_count, int32_t limit, int32_t offset) {
        std::string path = std::format("{}/plays/best?limit={}&offset={}{}", user_path(user), limit, offset, key_count_query(key_count, '&'));

        return call<std::vector<BestPlay>>(Auth::PUBLIC, "GET", path, nullptr, [](const json& j) {
            return get_list<BestPlay>(j, "plays", [](const json& entry) {
                return BestPlay{get_int(entry, "position"), get_double(entry, "weight"), get_double(entry, "weighted_performance"), parse_play(field(entry, "play"))};
            });
        });
    }

    std::future<ApiResult<RecentPlays>> ServerApi::recent_plays(const std::string& user, int32_t limit, std::optional<int64_t> before) {
        std::string path = std::format("{}/plays/recent?limit={}{}", user_path(user), limit, before ? std::format("&before={}", *before) : std::string());

        return call<RecentPlays>(Auth::VIEWER, "GET", path, nullptr, [](const json& j) {
            return RecentPlays{get_list<ServerPlay>(j, "plays", parse_play), get_optional_int(j, "next_before")};
        });
    }

    std::future<ApiResult<std::vector<ServerPlay>>> ServerApi::chart_plays(const std::string& user, const std::string& chart_hash, std::optional<float> rate) {
        std::string path = std::format("{}/charts/{}/plays{}", user_path(user), chart_hash, rate ? std::format("?rate={:.2f}", *rate) : std::string());

        return call<std::vector<ServerPlay>>(Auth::VIEWER, "GET", path, nullptr, [](const json& j) {
            return get_list<ServerPlay>(j, "plays", parse_play);
        });
    }

    std::future<ApiResult<ServerChart>> ServerApi::chart(const std::string& chart_hash) {
        return call<ServerChart>(Auth::PUBLIC, "GET", "/charts/" + chart_hash, nullptr, [](const json& j) {
            return parse_chart(field(j, "chart"));
        });
    }

    std::future<ApiResult<ChartList>> ServerApi::charts(const ChartQuery& query) {
        std::string path = std::format("/charts?sort={}&order={}&limit={}&offset={}", query.sort, query.descending ? "desc" : "asc", query.limit, query.offset);

        if (!query.search.empty()) {
            path += "&q=" + ServerClient::url_encode(query.search);
        }

        if (!query.status.empty()) {
            path += "&status=" + query.status;
        }

        if (!query.source.empty()) {
            path += "&source=" + ServerClient::url_encode(query.source);
        }

        for (const auto& [name, value] : {
                 std::pair{"min_difficulty", query.min_difficulty},
                 std::pair{"max_difficulty", query.max_difficulty},
                 std::pair{"min_duration", query.min_duration_s},
                 std::pair{"max_duration", query.max_duration_s},
                 std::pair{"min_notes", query.min_notes},
                 std::pair{"max_notes", query.max_notes},
             }) {
            if (value) {
                path += std::format("&{}={}", name, *value);
            }
        }

        path += key_count_query(query.key_count, '&');

        return call<ChartList>(Auth::PUBLIC, "GET", path, nullptr, [](const json& j) {
            return ChartList{get_int(j, "total"), get_list<ServerChart>(j, "charts", parse_chart)};
        });
    }

    std::future<ApiResult<ChartLeaderboard>> ServerApi::chart_leaderboard(const std::string& chart_hash, float rate, int32_t limit, int32_t offset) {
        std::string path = std::format("/charts/{}/leaderboard?rate={:.2f}&limit={}&offset={}", chart_hash, rate, limit, offset);

        return call<ChartLeaderboard>(Auth::VIEWER, "GET", path, nullptr, [](const json& j) {
            ChartLeaderboard board;
            board.chart = parse_chart(field(j, "chart"));
            board.rate = (float)get_double(j, "rate");
            board.total = get_int(j, "total");
            board.entries = get_list<RankedPlay>(j, "entries", parse_ranked_play);

            if (field(j, "viewer").is_object()) {
                board.viewer = parse_ranked_play(j["viewer"]);
            }

            return board;
        });
    }

    std::future<ApiResult<ServerChart>> ServerApi::upload_chart(const Chart& chart) {
        return call<ServerChart>(Auth::SIGNED_IN, "POST", "/charts", chart.serialize(), [](const json& j) {
            return parse_chart(field(j, "chart"));
        });
    }

    std::future<ApiResult<Leaderboard>> ServerApi::leaderboard(std::optional<uint8_t> key_count, int32_t limit, int32_t offset) {
        std::string path = std::format("/leaderboards/performance?limit={}&offset={}{}", limit, offset, key_count_query(key_count, '&'));

        return call<Leaderboard>(Auth::PUBLIC, "GET", path, nullptr, [](const json& j) {
            Leaderboard board;
            std::optional<int64_t> key_count = get_optional_int(j, "key_count");
            board.key_count = key_count ? std::optional<uint8_t>((uint8_t)*key_count) : std::nullopt;
            board.total = get_int(j, "total");
            board.entries = get_list<LeaderboardEntry>(j, "entries", [](const json& entry) {
                return LeaderboardEntry{get_int(entry, "rank"), parse_user(field(entry, "user")), get_double(entry, "performance"), get_double(entry, "accuracy"), get_int(entry, "ranked_play_count")};
            });
            return board;
        });
    }

    std::future<ApiResult<PlayTicket>> ServerApi::request_ticket(const Chart& chart) {
        json body = {{"chart_hash", chart.gameplay_hash()}, {"rate", chart.pitch}};

        return call<PlayTicket>(Auth::SIGNED_IN, "POST", "/plays/tickets", body, [](const json& j) {
            return PlayTicket{get_string(j, "ticket"), get_string(j, "chart_hash"), (float)get_double(j, "rate"), get_string(j, "expires_at"), get_bool(j, "chart_known"), get_string(j, "chart_status")};
        });
    }

    std::future<ApiResult<Submission>> ServerApi::submit_play(const Chart& chart, const Replay& replay, const std::string& ticket) {
        std::promise<ApiResult<PlayTicket>> ready;
        ApiResult<PlayTicket> issued;

        if (!ticket.empty()) {
            issued.value = PlayTicket();
            issued.value->ticket = ticket;
        }

        ready.set_value(issued);
        return submit_play(chart, replay, ready.get_future().share());
    }

    std::future<ApiResult<Submission>> ServerApi::submit_play(const Chart& chart, const Replay& replay, std::shared_future<ApiResult<PlayTicket>> ticket) {
        json body = {{"chart_hash", chart.gameplay_hash()}, {"replay", replay}};
        json chart_data = chart.serialize();

        return run_detached([body, chart_data, ticket]() mutable {
            if (ticket.valid() && ticket.get().ok() && !ticket.get().value->ticket.empty()) {
                body["ticket"] = ticket.get().value->ticket;
            }

            return send_submission(std::move(body), chart_data);
        });
    }

    ApiResult<Submission> ServerApi::send_submission(nlohmann::json body, const nlohmann::json& chart_data) {
        ApiResult<Submission> result;
        result.response = Account::authorized("POST", "/plays", body);

        if (result.response.status == 409 && result.response.error_code() == "chart_required") {
            body["chart"] = chart_data;
            result.response = Account::authorized("POST", "/plays", body);
        }

        if (result.response.ok()) {
            const json& j = result.response.body;
            Submission submission;
            submission.play = parse_play(field(j, "play"));
            submission.chart = parse_chart(field(j, "chart"));
            submission.chart_created = get_bool(j, "chart_created");
            submission.chart_rank = get_optional_int(j, "chart_rank");

            if (field(j, "key_mode").is_object()) {
                submission.key_mode = parse_standing(j["key_mode"]);
            }

            result.value = submission;
        }

        return result;
    }

    bool ServerApi::is_retryable(const ServerResponse& response) {
        return !response.error.empty() || response.status == 401 || response.status == 408 || response.status == 429 || response.status >= 500;
    }

    std::future<ApiResult<ServerPlay>> ServerApi::play(int64_t play_id) {
        return call<ServerPlay>(Auth::VIEWER, "GET", std::format("/plays/{}", play_id), nullptr, [](const json& j) {
            return parse_play(field(j, "play"));
        });
    }

    std::future<ApiResult<Replay>> ServerApi::replay(int64_t play_id) {
        return call<Replay>(Auth::VIEWER, "GET", std::format("/plays/{}/replay", play_id), nullptr, [](const json& j) {
            return j.get<Replay>();
        });
    }
}
