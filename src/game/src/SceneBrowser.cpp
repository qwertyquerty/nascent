#include "SceneBrowser.h"
#include "RichPresence.h"

#include "BeatmapDownloads.h"
#include "Game.h"
#include "InputManager.h"
#include "ProgressBar.h"
#include "RowField.h"
#include "SceneList.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "TextField.h"
#include "TextCache.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <utility>

namespace nascent {
    static constexpr int32_t FORM_GAP = 60;
    static constexpr int32_t ROW_PADDING = 16;
    static constexpr int32_t STATUS_WIDTH = 260;
    static constexpr int32_t FIELD_GAP = 10;
    static constexpr int32_t HINT_LINE_HEIGHT = 28;
    static constexpr uint8_t DOWNLOAD_TRACK_ALPHA = 12;
    static constexpr uint8_t DOWNLOAD_FILL_ALPHA = 70;
    static constexpr uint8_t FIRST_KEY_COUNT = 4;
    static constexpr uint8_t LAST_KEY_COUNT = 10;
    static constexpr float SCROLL_SPEED = 14.0f;
    static const olc::Pixel DOWNLOADED_COLOR = olc::Pixel(90, 220, 120);

    template <typename T>
    static bool is_ready(const std::future<T>& future) {
        return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }

    static int32_t text_width(olc::Font& font, const std::string& text) {
        return text.empty() ? 0 : text_bounds(font, text).size.x;
    }

    static std::string fit_text(olc::Font& font, std::string text, int32_t max_width) {
        if (text_width(font, text) <= max_width) {
            return text;
        }

        while (!text.empty() && text_width(font, text + "...") > max_width) {
            text.pop_back();

            while (!text.empty() && ((unsigned char)text.back() & 0xc0) == 0x80) {
                text.pop_back();
            }

            if (!text.empty() && (unsigned char)text.back() >= 0xc0) {
                text.pop_back();
            }
        }

        return text + "...";
    }

    static std::string format_length(int64_t milliseconds) {
        int64_t seconds = milliseconds / 1000;
        return std::format("{}:{:02}", seconds / 60, seconds % 60);
    }

    static std::optional<int64_t> osu_set_id(const ServerChart& chart) {
        for (const ServerChartSource& source : chart.sources) {
            if (source.source == "osu" && source.set_id) {
                return source.set_id;
            }
        }

        return std::nullopt;
    }

    SceneBrowser::SceneBrowser(Skin* skin, SceneList* list) : skin(skin), list(list) {}

    void SceneBrowser::init(Game*) {
        std::vector<std::string> key_options = {"Any"};

        for (uint8_t keys = FIRST_KEY_COUNT; keys <= LAST_KEY_COUNT; keys++) {
            key_options.push_back(std::format("{}K", keys));
        }

        form.spacing = FIELD_GAP;
        search_field = &form.add<TextField>("Title, artist or mapper");
        key_field = &form.add<ChoiceField>("Keys", key_options);
        status_field = &form.add<ChoiceField>("Status", std::vector<std::string>{"Ranked", "Unranked", "Any"});
        downloadable_field = &form.add<ChoiceField>("Downloadable only", std::vector<std::string>{"Yes", "No"});
        min_difficulty_field = &form.add<NumberField>("Min diff", "", 1.0, 1000.0);
        max_difficulty_field = &form.add<NumberField>("Max diff", "", 1.0, 1000.0);
        min_length_field = &form.add<NumberField>("Min length", "s", 30.0, 36000.0);
        max_length_field = &form.add<NumberField>("Max length", "s", 30.0, 36000.0);
        wanted = build_query();
    }

    void SceneBrowser::on_enter() {
        RichPresence::in_menu("Browsing for new charts");
        focus_results(false);
    }

    void SceneBrowser::on_exit() {
        form.set_enabled(game->window, false);
    }

    ChartQuery SceneBrowser::build_query() const {
        ChartQuery query;
        query.search = search_field->value;
        query.key_count = key_field->selected > 0 ? std::optional<uint8_t>((uint8_t)(FIRST_KEY_COUNT + key_field->selected - 1)) : std::nullopt;
        query.status = status_field->selected == 0 ? "ranked" : status_field->selected == 1 ? "unranked"
                                                                                            : "";
        query.source = downloadable_field->selected == 0 ? "osu" : "";
        query.min_difficulty = min_difficulty_field->value();
        query.max_difficulty = max_difficulty_field->value();
        query.min_duration_s = min_length_field->value();
        query.max_duration_s = max_length_field->value();
        query.limit = PAGE_SIZE;
        return query;
    }

    void SceneBrowser::request_page(const ChartQuery& query) {
        requested = query;
        pending = ServerApi::charts(query);
    }

    void SceneBrowser::poll_results() {
        if (!is_ready(pending) || !requested) {
            return;
        }

        ApiResult<ChartList> result = pending.get();
        ChartQuery first_page = *requested;
        bool appending = first_page.offset > 0;
        first_page.offset = 0;
        requested.reset();

        if (!result.ok()) {
            error = result.error();
            shown = first_page;
            return;
        }

        if (appending && shown == first_page) {
            results.insert(results.end(), result.value->charts.begin(), result.value->charts.end());
        }
        else {
            results = std::move(result.value->charts);
            selected = 0;
            scroll = 0;

            if (results.empty()) {
                focus_results(false);
            }
        }

        total = result.value->total;
        shown = first_page;
        error.clear();
    }

    void SceneBrowser::play_menu_sound() {
        game->play_sound(skin->get_random_menu_pluck());
    }

    void SceneBrowser::focus_results(bool focused) {
        results_focused = focused && !results.empty();
        form.set_enabled(game->window, !results_focused);
    }

    bool SceneBrowser::download_selected() {
        if (selected >= results.size()) {
            return false;
        }

        const ServerChart& chart = results[selected];
        std::optional<int64_t> set_id = osu_set_id(chart);

        if (list->get_local_hashes().contains(chart.hash)) {
            return false;
        }

        if (!set_id) {
            error = "This chart has no osu! beatmap to download";
            return false;
        }

        error.clear();
        BeatmapDownloads::start(*set_id, chart.artist, chart.title);
        return true;
    }

    void SceneBrowser::update(float elapsed_time) {
        olc::PixelGameEngine* window = game->window;

        if (InputManager::switch_pressed(window)) {
            bool was_focused = results_focused;
            focus_results(!results_focused);

            if (results_focused != was_focused) {
                play_menu_sound();
            }
        }
        else if (!results_focused) {
            UiForm::Action action = form.update(window, game->get_frame_text(), elapsed_time);

            if (action == UiForm::Action::SUBMIT) {
                focus_results(true);
            }

            if (action == UiForm::Action::MOVED || action == UiForm::Action::CHANGED) {
                game->play_sound(skin->sound(SkinSound::HITSOUND));
            }
            else if (action == UiForm::Action::SUBMIT) {
                play_menu_sound();
            }
        }
        else {
            size_t page = (size_t)std::max(1, (window->GetScreenSize().y / 2) / ROW_HEIGHT);
            int32_t wheel = window->GetMouseWheel();
            size_t previous = selected;

            if (window->GetKey(InputManager::menu_down_key).bPressed || wheel < 0) {
                selected = std::min(selected + 1, results.size() - 1);
            }
            else if (window->GetKey(InputManager::menu_up_key).bPressed || wheel > 0) {
                selected = selected > 0 ? selected - 1 : 0;
            }
            else if (window->GetKey(olc::Key::PGDN).bPressed) {
                selected = std::min(selected + page, results.size() - 1);
            }
            else if (window->GetKey(olc::Key::PGUP).bPressed) {
                selected = selected > page ? selected - page : 0;
            }
            else if (window->GetKey(InputManager::menu_start_key).bPressed && download_selected()) {
                play_menu_sound();
            }

            if (selected != previous) {
                game->play_sound(skin->sound(SkinSound::HITSOUND));
            }
        }

        ChartQuery query = build_query();

        if (query != wanted) {
            wanted = query;
            query_delay = QUERY_DELAY;
        }

        query_delay = std::max(0.0f, query_delay - elapsed_time);
        poll_results();

        if (!pending.valid() && query_delay <= 0.0f) {
            if (shown != wanted) {
                request_page(wanted);
            }
            else if ((int64_t)results.size() < total && selected + PREFETCH_ROWS >= results.size()) {
                ChartQuery next = wanted;
                next.offset = (int32_t)results.size();
                request_page(next);
            }
        }

        scroll += ((float)selected * ROW_HEIGHT - scroll) * std::min(1.0f, elapsed_time * SCROLL_SPEED);
    }

    void SceneBrowser::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_large, "Browse Charts", {x, y}, olc::WHITE);
        y += 90;

        std::string subtitle;

        if (!error.empty()) {
            subtitle = error;
        }
        else if (!shown) {
            subtitle = "Searching...";
        }
        else {
            subtitle = std::format("{} charts on {}", total, SettingsManager::settings.server_url);
        }

        draw_text(skin->display_font_small, subtitle, {x, y}, error.empty() ? olc::GREY : olc::RED);
        y += 60;

        form.draw(window, skin, {x, y});

        int32_t panel_x = x + TextField::WIDTH + FORM_GAP;
        int32_t panel_width = screen.x - panel_x - x;
        int32_t top = screen.y / 24;
        int32_t bottom = screen.y - screen.y / 24;
        float first = std::max(0.0f, scroll - (float)(screen.y / 2 - top - ROW_HEIGHT / 2));

        olc::Font& title_font = skin->display_font_medium;
        olc::Font& detail_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect detail_metrics = text_bounds(detail_font, "Hgjy");

        if (shown && results.empty() && error.empty()) {
            draw_text(detail_font, "No charts match these filters", {panel_x + ROW_PADDING, top - detail_metrics.offset.y}, olc::GREY);
        }

        for (size_t index = 0; index < results.size(); index++) {
            float row_top = top + (float)index * ROW_HEIGHT - first;

            if (row_top + ROW_HEIGHT <= 0 || row_top >= screen.y) {
                continue;
            }

            const ServerChart& chart = results[index];
            bool is_selected = index == selected;
            olc::Pixel color = is_selected ? olc::WHITE : olc::GREY;
            int32_t row_y = (int32_t)row_top;

            std::string status;
            olc::Pixel status_color = olc::GREY;
            std::optional<int64_t> set_id = osu_set_id(chart);
            std::shared_ptr<BeatmapDownloads::Download> download = set_id ? BeatmapDownloads::find(*set_id) : nullptr;
            BeatmapDownloads::State download_state = download != nullptr ? download->state.load() : BeatmapDownloads::State::FAILED;
            bool downloading = download != nullptr && (download_state == BeatmapDownloads::State::DOWNLOADING || download_state == BeatmapDownloads::State::EXTRACTING);
            olc::vf2d box_position = {(float)panel_x, row_top};
            olc::vf2d box_size = {(float)panel_width, (float)ROW_HEIGHT - 6};
            uint8_t highlight = is_selected ? (results_focused ? 36 : 16) : 0;

            if (downloading) {
                float progress = download_state == BeatmapDownloads::State::EXTRACTING ? 1.0f : (float)download->progress.load();
                ProgressBar bar(box_size);
                bar.track_color = olc::Pixel(255, 255, 255, std::max<uint8_t>(highlight, DOWNLOAD_TRACK_ALPHA));
                olc::Pixel fill = skin->title_gradient_at(progress, 0.5);
                fill.a = DOWNLOAD_FILL_ALPHA;
                bar.draw(window, box_position, progress, fill);
            }
            else if (is_selected) {
                window->FillRectDecal(box_position, box_size, olc::Pixel(255, 255, 255, highlight));
            }

            if (list->get_local_hashes().contains(chart.hash)) {
                status = "Downloaded";
                status_color = DOWNLOADED_COLOR;
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::DOWNLOADING) {
                status = std::format("Downloading {:.0f}%", download->progress.load() * 100.0);
                status_color = olc::WHITE;
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::EXTRACTING) {
                status = "Extracting";
                status_color = olc::WHITE;
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::DONE) {
                status = "Downloaded";
                status_color = DOWNLOADED_COLOR;
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::FAILED) {
                status = fit_text(detail_font, "Failed: " + download->error(), STATUS_WIDTH);
                status_color = olc::RED;
            }
            else if (!set_id) {
                status = "Not on osu!";
                status_color = olc::DARK_GREY;
            }
            else {
                status = is_selected && results_focused ? "Enter:  download" : "Download";
            }

            std::string title = std::format("{} - {} [{}]", chart.artist, chart.title, chart.difficulty_name);
            int32_t title_baseline = row_y + 8 - title_metrics.offset.y;
            draw_text(title_font, fit_text(title_font, title, panel_width - STATUS_WIDTH - ROW_PADDING * 3), {panel_x + ROW_PADDING, title_baseline}, color);

            std::string details = std::format(
                "{}K      {:.2f}      {}      {} notes      {}      mapped by {}",
                chart.key_count,
                chart.difficulty,
                format_length(chart.duration_ms),
                chart.note_count,
                chart.status,
                chart.mapper
            );
            int32_t detail_baseline = row_y + ROW_HEIGHT - 16 - (detail_metrics.size.y + detail_metrics.offset.y);
            draw_text(detail_font, fit_text(detail_font, details, panel_width - STATUS_WIDTH - ROW_PADDING * 3), {panel_x + ROW_PADDING, detail_baseline}, olc::GREY);

            int32_t status_x = panel_x + panel_width - ROW_PADDING - text_width(detail_font, status);
            draw_text(detail_font, status, {status_x, title_baseline}, status_color);
        }

        std::vector<std::string> hints = results_focused ? std::vector<std::string>{"↑/↓:  chart", "Enter:  download", "Ctrl+Tab:  filters", "Esc:  back"}
                                                         : std::vector<std::string>{"Tab, ↑/↓:  filter", "←/→:  change", "Enter:  next", "Ctrl+Tab:  results", "Esc:  back"};
        int32_t hint_baseline = bottom - (detail_metrics.size.y + detail_metrics.offset.y) - (int32_t)(hints.size() - 1) * HINT_LINE_HEIGHT;

        for (const std::string& hint : hints) {
            draw_text(detail_font, hint, {x, hint_baseline}, olc::GREY);
            hint_baseline += HINT_LINE_HEIGHT;
        }
    }
}
