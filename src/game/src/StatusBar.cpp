#include "StatusBar.h"

#include "Account.h"
#include "Skin.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <vector>
#include <utility>

namespace nascent {
    static const olc::Pixel RATE_CHANGED_COLOR = olc::Pixel(240, 190, 70);

    template <typename T>
    static bool is_ready(const std::future<T>& future) {
        return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    static std::string format_rank(std::optional<int64_t> rank) {
        return rank ? std::format("#{}", *rank) : std::string("Unranked");
    }

    static std::string format_playtime(int64_t milliseconds) {
        int64_t minutes = milliseconds / 60000;
        return minutes >= 60 ? std::format("{}h {}m", minutes / 60, minutes % 60) : std::format("{}m", minutes);
    }

    void StatusBar::refresh() {
        heartbeat_timer = 0;
        profile_timer = 0;
    }

    void StatusBar::set_rate(float rate) {
        this->rate = rate;
    }

    void StatusBar::set_key_count(uint8_t key_count) {
        this->key_count = key_count;
    }

    void StatusBar::update(float elapsed_time) {
        heartbeat_timer -= elapsed_time;
        profile_timer -= elapsed_time;
        poll_heartbeat();
        poll_profile();
    }

    void StatusBar::poll_heartbeat() {
        if (is_ready(heartbeat)) {
            ApiResult<ServerHealth> result = heartbeat.get();
            Connection previous = connection;

            if (result.ok()) {
                connection = result.value->healthy ? Connection::ONLINE : Connection::DEGRADED;
                latency_ms = result.value->latency_ms;
            }
            else {
                connection = Connection::OFFLINE;
            }

            if (previous == Connection::OFFLINE && connection != Connection::OFFLINE) {
                profile_timer = 0;
            }
        }

        if (!heartbeat.valid() && heartbeat_timer <= 0) {
            heartbeat = ServerApi::health();
            heartbeat_timer = HEARTBEAT_INTERVAL;
        }
    }

    void StatusBar::poll_profile() {
        std::string current = Account::signed_in() ? Account::username() : std::string();

        if (current != username) {
            username = current;
            profile.reset();
            profile_timer = 0;
        }

        if (is_ready(profile_request)) {
            ApiResult<Profile> result = profile_request.get();

            if (result.ok() && requested_username == username) {
                profile = std::move(*result.value);
            }
        }

        if (!username.empty() && !profile_request.valid() && profile_timer <= 0) {
            requested_username = username;
            profile_request = ServerApi::profile(username);
            profile_timer = PROFILE_INTERVAL;
        }
    }

    void StatusBar::draw(olc::PixelGameEngine* window, Skin* skin) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t top = screen.y - HEIGHT;
        olc::Font& font = skin->display_font_small;

        olc::Pixel::Mode mode = window->GetPixelMode();
        window->SetPixelMode(olc::Pixel::ALPHA);
        window->FillRect({0, top}, {screen.x, HEIGHT}, BACKGROUND);
        window->DrawLine({0, top}, {screen.x, top}, EDGE);
        window->SetPixelMode(mode);

        olc::FontRect metrics = font.GetStringBounds("Hgjy");
        int32_t baseline = top + (HEIGHT - metrics.size.y) / 2 - metrics.offset.y;
        int32_t x = PADDING;

        auto width = [&](const std::string& text) {
            return text.empty() ? 0 : font.GetStringBounds(text).size.x;
        };

        std::string status;
        olc::Pixel status_color;

        switch (connection) {
            case Connection::CHECKING:
                status = "Connecting";
                status_color = olc::Pixel(200, 200, 200);
                break;
            case Connection::ONLINE:
                status = std::format("Online  {:.0f} ms", latency_ms);
                status_color = olc::Pixel(90, 220, 120);
                break;
            case Connection::DEGRADED:
                status = "Server degraded";
                status_color = olc::Pixel(240, 190, 70);
                break;
            case Connection::OFFLINE:
                status = "Offline";
                status_color = olc::Pixel(235, 80, 80);
                break;
        }

        std::vector<std::string> hints = {"Shift ←/→  rate", "F3  calibration", username.empty() ? "F4  sign in" : "F4  account", "F5  browse"};
        int32_t hint_x = screen.x - PADDING + SEGMENT_GAP;

        for (auto hint = hints.rbegin(); hint != hints.rend(); ++hint) {
            hint_x -= SEGMENT_GAP + width(*hint);
            font.DrawString(*hint, {hint_x, baseline}, olc::GREY);
        }

        int32_t status_x = hint_x - SEGMENT_GAP - width(status);
        int32_t dot_x = status_x - LABEL_GAP - DOT_RADIUS * 2;
        font.DrawString(status, {status_x, baseline}, olc::WHITE);
        window->FillCircle({dot_x, top + HEIGHT / 2}, DOT_RADIUS, status_color);

        std::string rate_value = std::format("{:.2f}x", rate);
        int32_t rate_value_x = dot_x - DOT_RADIUS - SEGMENT_GAP - width(rate_value);
        int32_t rate_label_x = rate_value_x - LABEL_GAP - width("Rate");
        font.DrawString("Rate", {rate_label_x, baseline}, olc::GREY);
        font.DrawString(rate_value, {rate_value_x, baseline}, std::abs(rate - 1.0f) < 0.001f ? olc::WHITE : RATE_CHANGED_COLOR);
        int32_t left_limit = rate_label_x - SEGMENT_GAP;
        bool left_full = false;

        auto text = [&](const std::string& value, olc::Pixel color) {
            font.DrawString(value, {x, baseline}, color);
            x += width(value);
        };

        auto segment = [&](const std::string& label, const std::string& value) {
            left_full = left_full || x + SEGMENT_GAP + width(label) + LABEL_GAP + width(value) > left_limit;

            if (left_full) {
                return;
            }

            x += SEGMENT_GAP;
            text(label, olc::GREY);
            x += LABEL_GAP;
            text(value, olc::WHITE);
        };

        if (username.empty()) {
            text("Guest", olc::WHITE);
            x += SEGMENT_GAP;
            text("Scores are not submitted while signed out", olc::GREY);
        }
        else {
            text(username, olc::WHITE);

            if (profile) {
                const PlayerStats& stats = profile->stats;
                segment("Performance", std::format("{:.2f}", stats.performance));
                segment("Global", format_rank(stats.global_rank));

                if (key_count > 0) {
                    auto mode_stats = std::find_if(profile->key_modes.begin(), profile->key_modes.end(), [this](const KeyModeStats& entry) {
                        return entry.key_count == key_count;
                    });

                    if (mode_stats != profile->key_modes.end()) {
                        segment(std::format("{}K", key_count), std::format("{:.2f}  {}", mode_stats->stats.performance, format_rank(mode_stats->stats.global_rank)));
                    }
                    else {
                        segment(std::format("{}K", key_count), "Unranked");
                    }
                }

                segment("Accuracy", std::format("{:.2f}%", stats.accuracy * 100));
                segment("Ranked plays", std::format("{}", stats.ranked_play_count));
                segment("Playtime", format_playtime(stats.total_playtime_ms));
            }
            else {
                x += SEGMENT_GAP;
                text(connection == Connection::OFFLINE ? "Profile unavailable" : "Loading profile", olc::GREY);
            }
        }
    }
}
