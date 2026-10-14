#include "StatusBar.h"

#include "Account.h"
#include "Skin.h"
#include "TextCache.h"
#include "Ui.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <string_view>
#include <vector>
#include <utility>

namespace nascent {
    template <typename T>
    static bool is_ready(const std::future<T>& future) {
        return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    static std::string format_rank(std::optional<int64_t> rank) {
        return rank ? std::format("#{}", *rank) : std::string("Unranked");
    }

    void StatusBar::refresh() {
        heartbeat_timer = 0;
        profile_timer = 0;
    }

    void StatusBar::set_rate(float rate) {
        this->rate = rate;
    }

    void StatusBar::set_leaderboards_shown(bool shown) {
        leaderboards_shown = shown;
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

    int32_t StatusBar::height() {
        return Ui::px(HEIGHT);
    }

    int32_t StatusBar::padding() {
        return Ui::px(PADDING);
    }

    void StatusBar::draw(olc::PixelGameEngine* window, Skin* skin) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t bar_height = height();
        int32_t top = screen.y - bar_height;
        int32_t segment_gap = Ui::px(SEGMENT_GAP);
        int32_t label_gap = Ui::px(LABEL_GAP);
        int32_t radius = std::max(1, Ui::px(DOT_RADIUS));
        olc::Font& font = skin->display_font_small;

        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, (float)bar_height}, skin->color(SkinColor::STATUS_BAR_BACKGROUND));
        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, 1.0f}, skin->color(SkinColor::STATUS_BAR_EDGE));

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = top + (bar_height - metrics.size.y) / 2 - metrics.offset.y;
        int32_t x = padding();

        auto width = [&](std::string_view text) {
            return text.empty() ? 0 : text_bounds(font, text).size.x;
        };

        std::string status;
        olc::Pixel status_color;

        switch (connection) {
            case Connection::CHECKING:
                status = "Connecting";
                status_color = skin->color(SkinColor::STATUS_IDLE);
                break;
            case Connection::ONLINE:
                status = std::format("Online  {:.0f} ms", latency_ms);
                status_color = skin->color(SkinColor::POSITIVE);
                break;
            case Connection::DEGRADED:
                status = "Server degraded";
                status_color = skin->color(SkinColor::ATTENTION);
                break;
            case Connection::OFFLINE:
                status = "Offline";
                status_color = skin->color(SkinColor::NEGATIVE);
                break;
        }

        const std::array<std::string_view, 8> hints = {leaderboards_shown ? "Tab  preview" : "Tab  scores", "Shift ←/→  rate", "F1  options", "F3  calibration", username.empty() ? "F4  sign in" : "F4  account", "F5  browse", "F6  input", "F7  chart"};
        int32_t hint_x = screen.x - padding() + segment_gap;

        for (auto hint = hints.rbegin(); hint != hints.rend(); ++hint) {
            hint_x -= segment_gap + width(*hint);
            draw_text(font, *hint, {hint_x, baseline}, skin->color(SkinColor::TEXT_MUTED));
        }

        int32_t status_x = hint_x - segment_gap - width(status);
        int32_t dot_x = status_x - label_gap - radius * 2;
        draw_text(font, status, {status_x, baseline}, skin->color(SkinColor::TEXT));

        if (!dot || dot_radius != radius) {
            auto sprite = std::make_unique<olc::Sprite>(radius * 2 + 1, radius * 2 + 1);

            for (int32_t dy = -radius; dy <= radius; dy++) {
                for (int32_t dx = -radius; dx <= radius; dx++) {
                    sprite->SetPixel(dx + radius, dy + radius, dx * dx + dy * dy <= radius * radius ? olc::WHITE : olc::BLANK);
                }
            }

            dot = SpriteDecal(std::move(sprite), true);
            dot_radius = radius;
        }

        window->DrawDecal({(float)(dot_x - radius), (float)(top + bar_height / 2 - radius)}, dot.get(), {1.0f, 1.0f}, status_color);

        std::string rate_value = std::format("{:.2f}x", rate);
        int32_t rate_value_x = dot_x - radius - segment_gap - width(rate_value);
        int32_t rate_label_x = rate_value_x - label_gap - width("Rate");
        draw_text(font, "Rate", {rate_label_x, baseline}, skin->color(SkinColor::TEXT_MUTED));
        draw_text(font, rate_value, {rate_value_x, baseline}, skin->color(std::abs(rate - 1.0f) < 0.001f ? SkinColor::TEXT : SkinColor::ATTENTION));
        int32_t left_limit = rate_label_x - segment_gap;
        bool left_full = false;

        auto text = [&](const std::string& value, olc::Pixel color) {
            draw_text(font, value, {x, baseline}, color);
            x += width(value);
        };

        auto segment = [&](const std::string& label, const std::string& value) {
            left_full = left_full || x + segment_gap + width(label) + label_gap + width(value) > left_limit;

            if (left_full) {
                return;
            }

            x += segment_gap;
            text(label, skin->color(SkinColor::TEXT_MUTED));
            x += label_gap;
            text(value, skin->color(SkinColor::TEXT));
        };

        if (username.empty()) {
            text("Guest", skin->color(SkinColor::TEXT));
            x += segment_gap;
            text("Scores are not submitted while signed out", skin->color(SkinColor::TEXT_MUTED));
        }
        else {
            text(username, skin->color(SkinColor::TEXT));

            if (profile && key_count > 0) {
                auto mode_stats = std::find_if(profile->key_modes.begin(), profile->key_modes.end(), [this](const KeyModeStats& entry) {
                    return entry.key_count == key_count;
                });
                std::string mode_label = std::format("{}K", key_count);

                if (mode_stats != profile->key_modes.end() && mode_stats->global_rank) {
                    segment(mode_label, std::format("{:.2f}", mode_stats->performance));
                    segment("Rank", format_rank(mode_stats->global_rank));
                    segment("Accuracy", std::format("{:.2f}%", mode_stats->accuracy * 100));
                }
                else {
                    segment(mode_label, "Unranked");
                }
            }
            else if (!profile) {
                x += segment_gap;
                text(connection == Connection::OFFLINE ? "Profile unavailable" : "Loading profile", skin->color(SkinColor::TEXT_MUTED));
            }
        }
    }
}
