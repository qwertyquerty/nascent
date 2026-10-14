#include "LeaderboardPanel.h"

#include "Account.h"
#include "Results.h"
#include "Skin.h"
#include "TextCache.h"
#include "Ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace nascent {
    template <typename T>
    static bool ready(const std::optional<T>& request) {
        return request && request->result.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    std::string LeaderboardPanel::key(const std::string& gameplay_hash, float pitch) {
        return std::format("{}@{}", gameplay_hash, std::lround(pitch * 100.0f));
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

        if (!local_request && !local_scores.contains(board_key)) {
            long rate = std::lround(pitch * 100.0f);
            local_request = LeaderboardRequest<std::vector<LocalScore>>{board_key, generation, std::async(std::launch::async, [gameplay_hash, rate] {
                                                                            return LocalReplays::scores(gameplay_hash, rate);
                                                                        })};
        }

        if (!online_request && !online_scores.contains(board_key)) {
            online_request = LeaderboardRequest<ApiResult<ChartLeaderboard>>{board_key, generation, ServerApi::chart_leaderboard(gameplay_hash, pitch, ONLINE_LIMIT)};
        }
    }

    void LeaderboardPanel::draw(olc::PixelGameEngine* window, Skin* skin, const std::string& gameplay_hash, float pitch, olc::vi2d position, olc::vi2d size) {
        int32_t padding = Ui::px(PADDING);
        int32_t row_height = Ui::px(ROW_HEIGHT);
        int32_t header_height = Ui::px(HEADER_HEIGHT);
        int32_t left = position.x;
        int32_t width = size.x;
        int32_t top = position.y;
        int32_t bottom = position.y + size.y;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)(bottom - top)}, skin->color(SkinColor::PANEL_BACKGROUND));

        olc::Font& header_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect header_metrics = text_bounds(header_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t x = left + padding;
        int32_t right = left + width - padding;
        int32_t y = top + padding;
        std::string board_key = key(gameplay_hash, pitch);
        std::string own_name = Account::signed_in() ? Account::username() : std::string();

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

        auto row = [&](int64_t rank, double accuracy, std::string name, uint32_t combo, bool highlight) {
            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            Grade grade = grade_for_accuracy(accuracy);
            std::string combo_text = std::format("{}x", combo);
            int32_t combo_width = text_bounds(row_font, combo_text).size.x;
            int32_t name_x = x + Ui::px(NAME_OFFSET);
            int32_t name_width = right - combo_width - padding / 2 - name_x;

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

            draw_text(row_font, std::format("#{}", rank), {x, baseline}, skin->color(SkinColor::TEXT_MUTED));
            draw_text(row_font, grade_name(grade), {x + Ui::px(GRADE_OFFSET), baseline}, skin->grade_color(grade));
            draw_text(row_font, std::format("{:.2f}%", std::floor(accuracy * 10000.0) / 100.0), {x + Ui::px(ACCURACY_OFFSET), baseline}, skin->color(SkinColor::TEXT));
            draw_text(row_font, name, {name_x, baseline}, skin->color(highlight ? SkinColor::TEXT : SkinColor::TEXT_MUTED));
            draw_text(row_font, combo_text, {right - combo_width, baseline}, skin->color(SkinColor::TEXT_MUTED));
            y += row_height;
        };

        auto local = local_scores.find(board_key);
        header("Local", local != local_scores.end() && !local->second.empty() ? std::format("{} plays", local->second.size()) : std::string());

        if (local == local_scores.end()) {
            message("Loading...", skin->color(SkinColor::TEXT_MUTED));
        }
        else if (local->second.empty()) {
            message("No plays at this rate yet", skin->color(SkinColor::TEXT_FAINT));
        }
        else {
            for (size_t index = 0; index < local->second.size() && (int32_t)index < LOCAL_ROWS; index++) {
                const LocalScore& score = local->second[index];
                row((int64_t)index + 1, score.accuracy, score.played_at.substr(0, 10), score.max_combo, false);
            }
        }

        y = std::max(y, top + padding + header_height + row_height * LOCAL_ROWS) + Ui::px(SECTION_GAP);

        auto online = online_scores.find(board_key);
        const ChartLeaderboard* board = online != online_scores.end() && online->second.ok() ? &*online->second.value : nullptr;
        header("Online", board != nullptr && board->total > 0 ? std::format("{} players", board->total) : std::string());

        if (online == online_scores.end()) {
            message("Loading...", skin->color(SkinColor::TEXT_MUTED));
            return;
        }

        if (board == nullptr) {
            message(online->second.error(), skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        if (board->entries.empty()) {
            message("No plays at this rate yet", skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        bool viewer_listed = false;
        int32_t rows = (bottom - padding - y) / row_height;

        for (const RankedPlay& entry : board->entries) {
            bool own = entry.play.user && !own_name.empty() && entry.play.user->username == own_name;
            bool reserve = board->viewer && !viewer_listed && !own;

            if (rows - (reserve ? 1 : 0) <= 0) {
                break;
            }

            viewer_listed = viewer_listed || own;
            row(entry.rank, entry.play.accuracy, entry.play.user ? entry.play.user->username : std::string(), (uint32_t)entry.play.max_combo, own);
            rows--;
        }

        if (board->viewer && !viewer_listed && rows > 0) {
            row(board->viewer->rank, board->viewer->play.accuracy, own_name, (uint32_t)board->viewer->play.max_combo, true);
        }
    }
}
