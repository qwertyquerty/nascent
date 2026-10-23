#include "LeaderboardPanel.h"

#include "Account.h"
#include "Results.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace nascent {
    template <typename T>
    static bool ready(const std::optional<T>& request) {
        return request && is_ready(request->result);
    }

    int32_t LeaderboardSection::max_scroll() const {
        return std::max(0, count - rows);
    }

    void LeaderboardSection::clamp() {
        scroll = std::clamp(scroll, 0, max_scroll());
    }

    std::string LeaderboardPanel::key(const std::string& gameplay_hash, float pitch) const {
        if (mode == LeaderboardMode::PERFORMANCE) {
            return std::format("{}@performance", gameplay_hash);
        }

        return std::format("{}@{}", gameplay_hash, std::lround(pitch * 100.0f));
    }

    void LeaderboardPanel::set_mode(LeaderboardMode mode) {
        this->mode = mode;
    }

    LeaderboardMode LeaderboardPanel::get_mode() const {
        return mode;
    }

    bool LeaderboardPanel::scroll(olc::vf2d point, int32_t wheel) {
        for (LeaderboardSection* section : {&local_section, &online_section}) {
            if (wheel == 0 || !section->area.contains(point)) {
                continue;
            }

            section->scroll -= ((wheel > 0) - (wheel < 0)) * WHEEL_ROWS;
            section->clamp();
            return true;
        }

        return panel_area.contains(point);
    }

    bool LeaderboardPanel::update_scrollbars(Input& input) {
        for (LeaderboardSection* section : {&local_section, &online_section}) {
            int32_t limit = section->max_scroll();
            float position = limit > 0 ? (float)section->scroll / (float)limit : 0.0f;

            if (std::optional<float> moved = section->bar.update(input, position)) {
                section->scroll = (int32_t)std::lround(*moved * (float)limit);
                section->clamp();
                return true;
            }
        }

        return false;
    }

    bool LeaderboardPanel::take_mode_click(Input& input) {
        return spots.hovered(MODE_SPOT, input.mouse_position()) && spots.take_click(input) == MODE_SPOT;
    }

    void LeaderboardPanel::clear() {
        generation++;
        local_scores.clear();
        online_scores.clear();
    }

    void LeaderboardPanel::wait_for_selection() {
        delay = SELECTION_DELAY;
    }

    void LeaderboardPanel::show_now() {
        delay = 0;
    }

    void LeaderboardPanel::update(float elapsed_time, bool shown, const std::string& gameplay_hash, float pitch) {
        if (ready(local_request)) {
            std::vector<LocalScore> scores = local_request->result.get();

            if (local_request->generation == generation) {
                local_scores[local_request->key] = std::move(scores);
            }

            local_request.reset();
        }

        if (ready(online_request)) {
            ApiResult<ChartLeaderboard> board = online_request->result.get();

            if (online_request->generation == generation) {
                online_scores.insert_or_assign(online_request->key, std::move(board));
            }

            online_request.reset();
        }

        delay = std::max(0.0f, delay - elapsed_time);

        if (!shown || gameplay_hash.empty() || delay > 0) {
            return;
        }

        std::string board_key = key(gameplay_hash, pitch);

        bool performance = mode == LeaderboardMode::PERFORMANCE;

        if (!local_request && !local_scores.contains(board_key)) {
            long rate = std::lround(pitch * 100.0f);
            local_request = LeaderboardRequest<std::vector<LocalScore>>{board_key, generation, std::async(std::launch::async, [gameplay_hash, rate, performance] {
                                                                            std::vector<LocalScore> scores = performance ? LocalReplays::all_scores(gameplay_hash) : LocalReplays::scores(gameplay_hash, rate);

                                                                            if (performance) {
                                                                                std::stable_sort(scores.begin(), scores.end(), [](const LocalScore& a, const LocalScore& b) {
                                                                                    return a.performance > b.performance;
                                                                                });
                                                                            }

                                                                            return scores;
                                                                        })};
        }

        if (!online_request && !online_scores.contains(board_key)) {
            std::future<ApiResult<ChartLeaderboard>> board = performance ? ServerApi::chart_performance_leaderboard(gameplay_hash, ENTRY_LIMIT) : ServerApi::chart_leaderboard(gameplay_hash, pitch, ENTRY_LIMIT);
            online_request = LeaderboardRequest<ApiResult<ChartLeaderboard>>{board_key, generation, std::move(board)};
        }
    }

    std::optional<LeaderboardPick> LeaderboardPanel::take_click(Input& input) {
        std::optional<int32_t> clicked = spots.take_click(input);

        if (!clicked || *clicked < 0 || (size_t)*clicked >= picks.size() || spots.hovered(MODE_SPOT, input.mouse_position())) {
            return std::nullopt;
        }

        return picks[(size_t)*clicked];
    }

    void LeaderboardPanel::draw(olc::PixelGameEngine* window, Skin* skin, const std::string& gameplay_hash, float pitch, olc::vi2d position, olc::vi2d size) {
        spots.clear();
        picks.clear();

        int32_t padding = Ui::layout_px("leaderboard.padding");
        int32_t row_height = Ui::layout_px("leaderboard.row_height");
        int32_t header_height = Ui::layout_px("leaderboard.header_height");
        int32_t grade_offset = Ui::layout_px("leaderboard.grade_offset");
        int32_t accuracy_offset = Ui::layout_px("leaderboard.accuracy_offset");
        int32_t name_offset = Ui::layout_px("leaderboard.name_offset");
        int32_t left = position.x;
        int32_t width = size.x;
        int32_t top = position.y;
        int32_t bottom = position.y + size.y;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)(bottom - top)}, skin->color(SkinColor::PANEL_BACKGROUND));
        panel_area = {{(float)left, (float)top}, {(float)width, (float)(bottom - top)}};

        olc::Font& header_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect header_metrics = text_bounds(header_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t x = left + padding;
        int32_t right = left + width - padding;
        int32_t y = top + padding;
        std::string board_key = key(gameplay_hash, pitch);
        std::string own_name = Account::signed_in() ? Account::username() : std::string();
        bool performance = mode == LeaderboardMode::PERFORMANCE;
        int32_t scrollbar_width = std::max(1, Ui::layout_px("leaderboard.scrollbar_width"));

        if (board_key != scrolled_key) {
            scrolled_key = board_key;
            local_section.scroll = 0;
            online_section.scroll = 0;
        }

        std::string mode_hint = performance ? "Shift+Tab  accuracy at this rate" : "Shift+Tab  performance at every rate";
        int32_t hint_baseline = bottom - padding - row_height + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
        Rect hint_area = text_rect(row_font, mode_hint, {(float)x, (float)hint_baseline}).inflated((float)padding / 4.0f);
        spots.add(hint_area, MODE_SPOT);
        draw_text(row_font, mode_hint, {x, hint_baseline}, skin->color(hint_area.contains(window->GetMousePos()) ? SkinColor::TEXT : SkinColor::TEXT_FAINT));
        bottom -= row_height + padding / 2;

        auto header = [&](std::string_view title, std::string_view detail) {
            draw_text(header_font, title, {x, y - header_metrics.offset.y}, skin->color(SkinColor::TEXT));

            if (!detail.empty()) {
                int32_t detail_width = text_bounds(row_font, detail).size.x;
                draw_text(row_font, detail, {right - detail_width, y - header_metrics.offset.y}, skin->color(SkinColor::TEXT_MUTED));
            }

            y += header_height;
        };

        auto message = [&](std::string_view text, olc::Pixel color) {
            draw_text(row_font, text, {x, y - row_metrics.offset.y}, color);
            y += row_height;
        };

        auto section_bar = [&](LeaderboardSection& section, int32_t first_row_y) {
            section.area = {{(float)left, (float)first_row_y}, {(float)width, (float)(section.rows * row_height)}};
            section.clamp();

            if (section.max_scroll() <= 0) {
                section.bar.hide();
                return;
            }

            Rect track = {{(float)(left + width - padding / 2 - scrollbar_width), (float)first_row_y}, {(float)scrollbar_width, (float)(section.rows * row_height)}};
            section.bar.layout(track, (float)section.rows / (float)section.count);
            section.bar.draw(window, (float)section.scroll / (float)section.max_scroll(), skin->color(SkinColor::SCROLLBAR_TRACK), skin->color(SkinColor::SCROLLBAR_THUMB));
        };

        auto row = [&](int64_t rank, double accuracy, double value, std::string name, std::string right_text, bool highlight, LeaderboardPick pick) {
            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            Rect area = {{(float)(left + padding / 2), (float)y}, {(float)(width - padding), (float)row_height}};
            spots.add(area, (int32_t)picks.size());
            picks.push_back(std::move(pick));

            if (!highlight && area.contains(window->GetMousePos())) {
                olc::Pixel hover = skin->color(SkinColor::SELECTION_HIGHLIGHT);
                hover.a = (uint8_t)(hover.a / 2);
                window->FillRectDecal(area.position, area.size, hover);
            }

            Grade grade = grade_for_accuracy(accuracy);
            int32_t right_width = text_bounds(row_font, right_text).size.x;
            int32_t name_x = x + name_offset;
            int32_t name_width = right - right_width - padding / 2 - name_x;

            if (highlight) {
                window->FillRectDecal({(float)(left + padding / 2), (float)y}, {(float)(width - padding), (float)row_height}, skin->color(SkinColor::SELECTION_HIGHLIGHT));
            }

            while (!name.empty() && text_bounds(row_font, name).size.x > name_width) {
                name.pop_back();

                while (!name.empty() && ((unsigned char)name.back() & 0xc0) == 0x80) {
                    name.pop_back();
                }

                if (!name.empty() && (unsigned char)name.back() >= 0xc0) {
                    name.pop_back();
                }
            }

            draw_text(row_font, std::format("#{}", rank), {x, baseline}, skin->rank_color(rank));
            skin->draw_grade(grade, row_font, {(float)(x + grade_offset), (float)baseline});
            std::string value_text = performance ? std::format("{:.2f}", value) : std::format("{:.2f}%", std::floor(accuracy * 10000.0) / 100.0);
            draw_text(row_font, value_text, {x + accuracy_offset, baseline}, skin->color(SkinColor::TEXT));
            draw_text(row_font, name, {name_x, baseline}, skin->color(highlight ? SkinColor::TEXT : SkinColor::TEXT_MUTED));
            draw_text(row_font, right_text, {right - right_width, baseline}, skin->color(SkinColor::TEXT_MUTED));
            y += row_height;
        };

        auto right_text = [&](uint32_t combo, float rate) {
            return performance ? std::format("{:.2f}x", rate) : std::format("{}x", combo);
        };

        std::string_view empty_text = performance ? "No plays yet" : "No plays at this rate yet";
        auto local = local_scores.find(board_key);
        header(performance ? "Local performance" : "Local", local != local_scores.end() && !local->second.empty() ? std::format("{} plays", local->second.size()) : std::string());
        int32_t local_top = y;
        local_section.rows = LOCAL_ROWS;
        local_section.count = 0;

        if (local == local_scores.end()) {
            message("Loading...", skin->color(SkinColor::TEXT_MUTED));
        }
        else if (local->second.empty()) {
            message(empty_text, skin->color(SkinColor::TEXT_FAINT));
        }
        else {
            local_section.count = (int32_t)std::min(local->second.size(), (size_t)ENTRY_LIMIT);
            local_section.clamp();

            for (int32_t index = local_section.scroll; index < local_section.count && index < local_section.scroll + LOCAL_ROWS; index++) {
                const LocalScore& score = local->second[(size_t)index];
                std::string date = score.played_at.substr(0, 10);
                row((int64_t)index + 1, score.accuracy, score.performance, date, right_text(score.max_combo, score.rate), false, {score.path, 0, std::format("your play from {}", date)});
            }
        }

        section_bar(local_section, local_top);
        y = std::max(y, top + padding + header_height + row_height * LOCAL_ROWS) + Ui::layout_px("leaderboard.section_gap");

        auto online = online_scores.find(board_key);
        const ChartLeaderboard* board = online != online_scores.end() && online->second.ok() ? &*online->second.value : nullptr;
        header(performance ? "Online performance" : "Online", board != nullptr && board->total > 0 ? std::format("{} players", board->total) : std::string());
        online_section.count = 0;
        online_section.rows = 0;
        online_section.bar.hide();
        online_section.area = {};

        if (online == online_scores.end()) {
            message("Loading...", skin->color(SkinColor::TEXT_MUTED));
            return;
        }

        if (board == nullptr) {
            message(online->second.error(), skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        if (board->entries.empty()) {
            message(empty_text, skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        auto is_own = [&](const RankedPlay& entry) {
            return entry.play.user && !own_name.empty() && entry.play.user->username == own_name;
        };

        bool pin_viewer = board->viewer && std::none_of(board->entries.begin(), board->entries.end(), is_own);
        int32_t rows = (bottom - y) / row_height - (pin_viewer ? 1 : 0);

        if (rows <= 0) {
            return;
        }

        int32_t online_top = y;
        online_section.rows = rows;
        online_section.count = (int32_t)board->entries.size();
        online_section.clamp();

        for (int32_t index = online_section.scroll; index < online_section.count && index < online_section.scroll + rows; index++) {
            const RankedPlay& entry = board->entries[(size_t)index];
            std::string player = entry.play.user ? entry.play.user->username : std::string();
            row(entry.rank, entry.play.accuracy, entry.play.performance, player, right_text((uint32_t)entry.play.max_combo, entry.play.rate), is_own(entry), {std::nullopt, entry.play.id, std::format("{} (#{})", player, entry.rank)});
        }

        section_bar(online_section, online_top);

        if (pin_viewer) {
            y = online_top + rows * row_height;
            const RankedPlay& viewer = *board->viewer;
            row(viewer.rank, viewer.play.accuracy, viewer.play.performance, own_name, right_text((uint32_t)viewer.play.max_combo, viewer.play.rate), true, {std::nullopt, viewer.play.id, std::format("{} (#{})", own_name, viewer.rank)});
        }
    }
}
