#include "SceneBrowser.h"
#include "RichPresence.h"

#include "BeatmapDownloads.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "gse/ui/ProgressBar.h"
#include "gse/ui/RowField.h"
#include "KeyBindField.h"
#include "SceneList.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "gse/ui/TextField.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "gse/render/Color.h"
#include "gse/concurrency/Async.h"

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
    static constexpr uint8_t DOWNLOAD_FILL_ALPHA = 70;
    static constexpr uint8_t FIRST_KEY_COUNT = 4;
    static constexpr uint8_t LAST_KEY_COUNT = 10;
    static constexpr float SCROLL_SPEED = 14.0f;

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

    void SceneBrowser::on_create() {
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
        form.set_enabled(false);
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
        game->audio().play_effect(skin->get_random_menu_pluck());
    }

    void SceneBrowser::focus_results(bool focused) {
        results_focused = focused && !results.empty();
        form.set_enabled(!results_focused);
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
        Input& input = game->input();

        if (InputManager::switch_pressed(input)) {
            bool was_focused = results_focused;
            focus_results(!results_focused);

            if (results_focused != was_focused) {
                play_menu_sound();
            }
        }
        else if (!results_focused) {
            UiForm::Action action = form.update(input, elapsed_time);

            if (action == UiForm::Action::SUBMIT) {
                focus_results(true);
            }

            if (action == UiForm::Action::MOVED || action == UiForm::Action::CHANGED) {
                game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            }
            else if (action == UiForm::Action::SUBMIT) {
                play_menu_sound();
            }
        }
        else {
            size_t page = (size_t)std::max(1, (window->GetScreenSize().y / 2) / std::max(1, Ui::px(ROW_HEIGHT)));
            int32_t wheel = input.mouse_wheel();
            size_t previous = selected;

            if (input.take(actions::DOWN) || wheel < 0) {
                selected = std::min(selected + 1, results.size() - 1);
            }
            else if (input.take(actions::UP) || wheel > 0) {
                selected = selected > 0 ? selected - 1 : 0;
            }
            else if (input.key_pressed(olc::Key::PGDN)) {
                selected = std::min(selected + page, results.size() - 1);
            }
            else if (input.key_pressed(olc::Key::PGUP)) {
                selected = selected > page ? selected - page : 0;
            }
            else if (input.take(actions::CONFIRM) && download_selected()) {
                play_menu_sound();
            }

            if (selected != previous) {
                game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
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

        scroll += ((float)selected * Ui::px(ROW_HEIGHT) - scroll) * std::min(1.0f, elapsed_time * SCROLL_SPEED);
    }

    void SceneBrowser::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_large, "Browse Charts", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::px(90);

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

        draw_text(skin->display_font_small, subtitle, {x, y}, skin->color(error.empty() ? SkinColor::TEXT_MUTED : SkinColor::TEXT_ERROR));
        y += Ui::px(60);

        form.draw(window, *skin, {x, y});

        int32_t row_height = Ui::px(ROW_HEIGHT);
        int32_t row_padding = Ui::px(ROW_PADDING);
        int32_t status_width = Ui::px(STATUS_WIDTH);
        int32_t panel_x = x + TextField::width() + Ui::px(FORM_GAP);
        int32_t panel_width = screen.x - panel_x - x;
        int32_t top = screen.y / 24;
        int32_t bottom = screen.y - screen.y / 24;
        float first = std::max(0.0f, scroll - (float)(screen.y / 2 - top - row_height / 2));

        olc::Font& title_font = skin->display_font_medium;
        olc::Font& detail_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect detail_metrics = text_bounds(detail_font, "Hgjy");

        if (shown && results.empty() && error.empty()) {
            draw_text(detail_font, "No charts match these filters", {panel_x + row_padding, top - detail_metrics.offset.y}, skin->color(SkinColor::TEXT_MUTED));
        }

        for (size_t index = 0; index < results.size(); index++) {
            float row_top = top + (float)index * row_height - first;

            if (row_top + row_height <= 0 || row_top >= screen.y) {
                continue;
            }

            const ServerChart& chart = results[index];
            bool is_selected = index == selected;
            olc::Pixel color = skin->color(is_selected ? SkinColor::TEXT : SkinColor::TEXT_MUTED);
            int32_t row_y = (int32_t)row_top;

            std::string status;
            olc::Pixel status_color = skin->color(SkinColor::TEXT_MUTED);
            std::optional<int64_t> set_id = osu_set_id(chart);
            std::shared_ptr<BeatmapDownloads::Download> download = set_id ? BeatmapDownloads::find(*set_id) : nullptr;
            BeatmapDownloads::State download_state = download != nullptr ? download->state.load() : BeatmapDownloads::State::FAILED;
            bool downloading = download != nullptr && (download_state == BeatmapDownloads::State::DOWNLOADING || download_state == BeatmapDownloads::State::EXTRACTING);
            olc::vf2d box_position = {(float)panel_x, row_top};
            olc::vf2d box_size = {(float)panel_width, (float)row_height - Ui::pxf(6)};
            uint8_t highlight = is_selected ? (results_focused ? 36 : 16) : 0;
            olc::Pixel selection = fade(skin->color(SkinColor::BROWSER_HIGHLIGHT), highlight / 255.0);

            if (downloading) {
                float progress = download_state == BeatmapDownloads::State::EXTRACTING ? 1.0f : (float)download->progress.load();
                ProgressBar bar(box_size, skin->color(SkinColor::DOWNLOAD_TRACK));
                bar.track_color.a = std::max(bar.track_color.a, selection.a);
                olc::Pixel fill = skin->title_gradient_at(progress, 0.5);
                fill.a = DOWNLOAD_FILL_ALPHA;
                bar.draw(window, box_position, progress, fill);
            }
            else if (is_selected) {
                window->FillRectDecal(box_position, box_size, selection);
            }

            if (list->get_local_hashes().contains(chart.hash)) {
                status = "Downloaded";
                status_color = skin->color(SkinColor::POSITIVE);
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::DOWNLOADING) {
                status = std::format("Downloading {:.0f}%", download->progress.load() * 100.0);
                status_color = skin->color(SkinColor::TEXT);
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::EXTRACTING) {
                status = "Extracting";
                status_color = skin->color(SkinColor::TEXT);
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::DONE) {
                status = "Downloaded";
                status_color = skin->color(SkinColor::POSITIVE);
            }
            else if (download != nullptr && download_state == BeatmapDownloads::State::FAILED) {
                status = fit_text(detail_font, "Failed: " + download->error(), status_width);
                status_color = skin->color(SkinColor::TEXT_ERROR);
            }
            else if (!set_id) {
                status = "Not on osu!";
                status_color = skin->color(SkinColor::TEXT_FAINT);
            }
            else {
                status = is_selected && results_focused ? "Enter:  download" : "Download";
            }

            std::string title = std::format("{} - {} [{}]", chart.artist, chart.title, chart.difficulty_name);
            int32_t title_baseline = row_y + Ui::px(8) - title_metrics.offset.y;
            draw_text(title_font, fit_text(title_font, title, panel_width - status_width - row_padding * 3), {panel_x + row_padding, title_baseline}, color);

            std::string keys = std::format("{}K      ", chart.key_count);
            std::string difficulty = std::format("{:.2f}", chart.difficulty);
            std::string details = std::format("      {}      {} notes      {}      mapped by {}", format_length(chart.duration_ms), chart.note_count, chart.status, chart.mapper);
            int32_t detail_baseline = row_y + row_height - Ui::px(16) - (detail_metrics.size.y + detail_metrics.offset.y);
            int32_t detail_x = panel_x + row_padding;
            int32_t detail_room = panel_width - status_width - row_padding * 3;
            draw_text(detail_font, keys, {detail_x, detail_baseline}, skin->color(SkinColor::TEXT_MUTED));
            detail_x += text_width(detail_font, keys);
            draw_text(detail_font, difficulty, {detail_x, detail_baseline}, skin->difficulty_color(chart.difficulty));
            detail_x += text_width(detail_font, difficulty);
            draw_text(detail_font, fit_text(detail_font, details, std::max(0, detail_room - (detail_x - panel_x - row_padding))), {detail_x, detail_baseline}, skin->color(SkinColor::TEXT_MUTED));

            int32_t status_x = panel_x + panel_width - row_padding - text_width(detail_font, status);
            draw_text(detail_font, status, {status_x, title_baseline}, status_color);
        }

        std::vector<std::string> hints = results_focused ? std::vector<std::string>{"↑/↓:  chart", "Enter:  download", "Ctrl+Tab:  filters", "Esc:  back"}
                                                         : std::vector<std::string>{"Tab, ↑/↓:  filter", "←/→:  change", "Enter:  next", "Ctrl+Tab:  results", "Esc:  back"};
        int32_t hint_line_height = Ui::px(HINT_LINE_HEIGHT);
        int32_t hint_baseline = bottom - (detail_metrics.size.y + detail_metrics.offset.y) - (int32_t)(hints.size() - 1) * hint_line_height;

        for (const std::string& hint : hints) {
            draw_text(detail_font, hint, {x, hint_baseline}, skin->color(SkinColor::TEXT_MUTED));
            hint_baseline += hint_line_height;
        }
    }
}
