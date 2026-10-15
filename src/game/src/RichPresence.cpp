#include "RichPresence.h"

#include "Account.h"
#include "Chart.h"
#include "ServerApi.h"
#include "SettingsManager.h"
#include "discord-rpc.hpp"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <future>
#include <optional>
#include <utility>

namespace nascent {
    namespace {
        struct Activity {
            std::string details;
            uint8_t key_count = 0;
            int64_t start = 0;
        };

        struct PresenceState {
            bool enabled = false;
            bool dirty = true;
            float since_push = RichPresence::MIN_UPDATE_INTERVAL;
            float profile_timer = 0;
            int64_t session_start = (int64_t)std::time(nullptr);
            Activity activity = {"On the title screen", 0, 0};
            std::string username;
            std::string requested_username;
            std::optional<Profile> profile;
            std::future<ApiResult<Profile>> profile_request;
        };

        PresenceState& presence() {
            static PresenceState value;
            return value;
        }

        std::string fit(std::string text) {
            if (text.size() <= RichPresence::MAX_TEXT_LENGTH) {
                return text;
            }

            size_t end = RichPresence::MAX_TEXT_LENGTH - 3;

            while (end > 0 && ((unsigned char)text[end] & 0xC0) == 0x80) {
                end--;
            }

            return text.substr(0, end) + "...";
        }

        std::string chart_name(const Chart& chart) {
            std::string name = std::format("{} - {} [{}]", chart.info.artist, chart.info.title, chart.info.difficulty_name);
            return std::abs(chart.pitch - 1.0f) > 0.001f ? std::format("{} {:.2f}x", name, chart.pitch) : name;
        }

        const KeyModeStats* shown_mode(const Profile& profile, uint8_t key_count) {
            const KeyModeStats* best = nullptr;

            for (const KeyModeStats& mode : profile.key_modes) {
                if (key_count != 0 && mode.key_count == key_count) {
                    return &mode;
                }

                if (key_count == 0 && mode.global_rank && (best == nullptr || mode.performance > best->performance)) {
                    best = &mode;
                }
            }

            return best;
        }

        std::string player_line() {
            const PresenceState& state = presence();

            if (state.username.empty()) {
                return "Playing as a guest";
            }

            const KeyModeStats* mode = state.profile ? shown_mode(*state.profile, state.activity.key_count) : nullptr;

            if (mode != nullptr && mode->global_rank) {
                return std::format("{} - {}K #{} ({:.2f})", state.username, mode->key_count, *mode->global_rank, mode->performance);
            }

            if (state.profile && state.activity.key_count != 0) {
                return std::format("{} - {}K unranked", state.username, state.activity.key_count);
            }

            return state.username;
        }

        void push() {
            PresenceState& state = presence();

            discord::RPCManager::get()
                .getPresence()
                .setDetails(fit(state.activity.details))
                .setState(fit(player_line()))
                .setStartTimestamp(state.activity.start != 0 ? state.activity.start : state.session_start)
                .setLargeImageKey(RichPresence::IMAGE_KEY)
                .setLargeImageText("Nascent")
                .refresh();

            state.dirty = false;
            state.since_push = 0;
        }

        void set_activity(std::string details, uint8_t key_count, int64_t start) {
            PresenceState& state = presence();

            if (state.activity.details == details && state.activity.key_count == key_count && start == 0) {
                return;
            }

            state.activity = {std::move(details), key_count, start};
            state.dirty = true;
        }

        void poll_profile(float elapsed_time) {
            PresenceState& state = presence();
            std::string current = Account::signed_in() ? Account::username() : std::string();

            if (current != state.username) {
                state.username = current;
                state.profile.reset();
                state.profile_timer = 0;
                state.dirty = true;
            }

            if (is_ready(state.profile_request)) {
                ApiResult<Profile> result = state.profile_request.get();

                if (result.ok() && state.requested_username == state.username) {
                    state.profile = std::move(*result.value);
                    state.dirty = true;
                }
            }

            state.profile_timer -= elapsed_time;

            if (!state.username.empty() && !state.profile_request.valid() && state.profile_timer <= 0) {
                state.requested_username = state.username;
                state.profile_request = ServerApi::profile(state.username);
                state.profile_timer = RichPresence::PROFILE_INTERVAL;
            }
        }
    }

    void RichPresence::set_enabled(bool enabled) {
        PresenceState& state = presence();

        if (enabled == state.enabled) {
            return;
        }

        state.enabled = enabled;

        if (enabled) {
            discord::RPCManager::get().setClientID(SettingsManager::settings.discord_application_id).initialize();
            state.dirty = true;
            state.since_push = MIN_UPDATE_INTERVAL;
            state.profile_timer = 0;
            return;
        }

        discord::RPCManager::get().clearPresence();
        discord::RPCManager::get().shutdown();
    }

    void RichPresence::update(float elapsed_time) {
        PresenceState& state = presence();

        if (!state.enabled) {
            return;
        }

        state.since_push += elapsed_time;
        poll_profile(elapsed_time);

        if (state.dirty && state.since_push >= MIN_UPDATE_INTERVAL) {
            push();
        }
    }

    void RichPresence::shutdown() {
        if (presence().enabled) {
            discord::RPCManager::get().shutdown();
            presence().enabled = false;
        }
    }

    void RichPresence::refresh_profile() {
        presence().profile_timer = 0;
    }

    void RichPresence::in_menu(std::string activity, uint8_t key_count) {
        set_activity(std::move(activity), key_count, 0);
    }

    void RichPresence::picking_song(const Chart* chart) {
        set_activity("Picking a song", chart != nullptr ? chart->info.key_count : 0, 0);
    }

    void RichPresence::playing(const Chart& chart) {
        set_activity(chart_name(chart), chart.info.key_count, (int64_t)std::time(nullptr));
    }

    void RichPresence::viewing_results(const Chart& chart, double accuracy, std::string_view grade) {
        set_activity(std::format("{} {:.2f}% on {}", grade, std::floor(accuracy * 10000.0) / 100.0, chart_name(chart)), chart.info.key_count, (int64_t)std::time(nullptr));
    }
}
