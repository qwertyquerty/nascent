#include "SceneBrowser.h"
#include "RichPresence.h"

#include "BeatmapDownloads.h"
#include "ConfirmOverlay.h"
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
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <memory>
#include <string_view>
#include <utility>

namespace nascent {
    static constexpr uint8_t FIRST_KEY_COUNT = 4;
    static constexpr uint8_t LAST_KEY_COUNT = 10;
    static constexpr std::array<std::string_view, 3> SOURCE_VALUES = {"", "osu", "etterna"};

    struct BrowserSort {
        std::string_view label;
        std::string_view key;
        bool descending;
    };

    static constexpr std::array<BrowserSort, 7> BROWSER_SORTS = {{
        {"Title", "title", false},
        {"Artist", "artist", false},
        {"Difficulty", "difficulty", false},
        {"Length", "length", false},
        {"Plays", "play_count", true},
        {"Newest", "created_at", true},
        {"Keys", "key_count", false},
    }};

    static int32_t text_width(olc::Font& font, const std::string& text) {
        return text.empty() ? 0 : text_bounds(font, text).size.x;
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

    static std::optional<int64_t> etterna_pack_id(const ServerChart& chart) {
        for (const ServerChartSource& source : chart.sources) {
            if (source.source == "etterna" && source.set_id) {
                return source.set_id;
            }
        }

        return std::nullopt;
    }

    static std::shared_ptr<BeatmapDownloads::Download> download_for(const ServerChart& chart) {
        if (std::optional<int64_t> set_id = osu_set_id(chart)) {
            return BeatmapDownloads::find(*set_id);
        }

        std::optional<int64_t> pack_id = etterna_pack_id(chart);
        return pack_id ? BeatmapDownloads::find_etterna_pack(*pack_id) : nullptr;
    }

    SceneBrowser::SceneBrowser(Skin* skin, SceneList* list) : skin(skin), list(list) {}

    void SceneBrowser::on_create() {
        std::vector<std::string> key_options = {"Any"};

        for (uint8_t keys = FIRST_KEY_COUNT; keys <= LAST_KEY_COUNT; keys++) {
            key_options.push_back(std::format("{}K", keys));
        }

        form.spacing = Ui::layout("browser.field_gap");
        search_field = &form.add<TextField>("Title, artist or mapper");
        key_field = &form.add<ChoiceField>("Keys", key_options);
        source_field = &form.add<ChoiceField>("Source", std::vector<std::string>{"Any", "osu!", "Etterna"});
        std::vector<std::string> sort_options;

        for (const BrowserSort& sort : BROWSER_SORTS) {
            sort_options.emplace_back(sort.label);
        }

        sort_field = &form.add<ChoiceField>("Sort by", sort_options);
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
        query.source = std::string(SOURCE_VALUES[std::min<size_t>(source_field->selected, SOURCE_VALUES.size() - 1)]);
        query.downloadable = true;
        query.min_difficulty = min_difficulty_field->value();
        query.max_difficulty = max_difficulty_field->value();
        query.min_duration_s = min_length_field->value();
        const BrowserSort& sort = BROWSER_SORTS[std::min<size_t>(sort_field->selected, BROWSER_SORTS.size() - 1)];
        query.sort = std::string(sort.key);
        query.descending = sort.descending;
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

        std::vector<ServerChart> page = std::move(result.value->charts);
        size_t received = page.size();
        std::erase_if(page, [](const ServerChart& chart) {
            return !osu_set_id(chart) && !etterna_pack_id(chart);
        });

        if (appending && shown == first_page) {
            results.insert(results.end(), page.begin(), page.end());
            fetched += received;
        }
        else {
            results = std::move(page);
            fetched = received;
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

        if (set_id) {
            error.clear();
            notice.clear();
            BeatmapDownloads::start(*set_id, chart.artist, chart.title);
            return true;
        }

        std::optional<int64_t> pack_id = etterna_pack_id(chart);

        if (!pack_id) {
            error = "This chart has no osu! beatmap or EtternaOnline pack to download";
            return false;
        }

        error.clear();
        std::shared_ptr<BeatmapDownloads::Download> existing = BeatmapDownloads::find_etterna_pack(*pack_id);

        if ((existing != nullptr && existing->state != BeatmapDownloads::State::FAILED) || pack_lookup.valid()) {
            return false;
        }

        pack_lookup_hash = chart.hash;
        notice = "Looking up the pack on EtternaOnline...";
        pack_lookup = run_detached([id = *pack_id] {
            return BeatmapDownloads::lookup_etterna_pack(id);
        });
        return true;
    }

    void SceneBrowser::poll_pack_lookup() {
        if (pack_lookup.valid() && is_ready(pack_lookup)) {
            BeatmapDownloads::PackLookup lookup = pack_lookup.get();
            notice.clear();

            if (!lookup.pack) {
                error = lookup.error;
            }
            else if (selected < results.size() && results[selected].hash == pack_lookup_hash) {
                BeatmapDownloads::EtternaPack pack = *lookup.pack;
                std::string songs = pack.song_count == 1 ? std::string("1 song") : std::format("{} songs", pack.song_count);
                std::string details = pack.size.empty() ? songs : std::format("{}, {}", songs, pack.size);
                std::vector<std::string> lines = {pack.name, details, "This chart only comes as part of this EtternaOnline pack"};
                overlays.open(std::make_unique<ConfirmOverlay>(skin, "Download the whole pack?", std::move(lines), "Download", [pack] {
                    BeatmapDownloads::start_etterna_pack(pack);
                }));
            }
        }
    }

    bool SceneBrowser::is_downloading(size_t index) const {
        if (index >= results.size()) {
            return false;
        }

        std::shared_ptr<BeatmapDownloads::Download> download = download_for(results[index]);
        return download != nullptr && download->state == BeatmapDownloads::State::DOWNLOADING;
    }

    bool SceneBrowser::cancel_download(size_t index) {
        if (index >= results.size()) {
            return false;
        }

        if (std::optional<int64_t> set_id = osu_set_id(results[index])) {
            return BeatmapDownloads::cancel(*set_id);
        }

        std::optional<int64_t> pack_id = etterna_pack_id(results[index]);
        return pack_id && BeatmapDownloads::cancel_etterna_pack(*pack_id);
    }

    bool SceneBrowser::update_mouse(Input& input) {
        if (std::optional<int32_t> hint = hint_spots.take_click(input)) {
            if (*hint == HINT_BACK) {
                input.inject(actions::BACK);
            }
            else if (*hint == HINT_CONFIRM) {
                input.inject(actions::CONFIRM);
            }
            else {
                bool was_focused = results_focused;
                focus_results(!results_focused);

                if (results_focused != was_focused) {
                    play_menu_sound();
                }

                return true;
            }

            return false;
        }

        if (results_focused && input.clicked(0) && form.contains(input.mouse_position())) {
            focus_results(false);
            play_menu_sound();
            return false;
        }

        if (std::optional<int32_t> cancelled = result_spots.take_click(input, 1)) {
            if (cancel_download((size_t)*cancelled)) {
                play_menu_sound();
            }

            return true;
        }

        std::optional<int32_t> row = result_spots.take_click(input);

        if (!row || (size_t)*row >= results.size()) {
            return false;
        }

        if (results_focused && selected == (size_t)*row) {
            if (download_selected()) {
                play_menu_sound();
            }

            return true;
        }

        selected = (size_t)*row;
        focus_results(true);
        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        return true;
    }

    void SceneBrowser::update(float elapsed_time) {
        olc::PixelGameEngine* window = game->window;
        Input& input = game->input();
        bool clicked = update_mouse(input);

        if (!clicked && InputManager::switch_pressed(input)) {
            bool was_focused = results_focused;
            focus_results(!results_focused);

            if (results_focused != was_focused) {
                play_menu_sound();
            }
        }
        else if (!clicked && !results_focused) {
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
        else if (!clicked) {
            size_t page = (size_t)std::max(1, (window->GetScreenSize().y / 2) / std::max(1, Ui::layout_px("browser.row_height")));
            int32_t wheel = input.mouse_wheel();
            size_t previous = selected;
            bool down = down_repeat.update(input.take(actions::DOWN), input.held(actions::DOWN), elapsed_time);
            bool up = up_repeat.update(input.take(actions::UP), input.held(actions::UP), elapsed_time);

            if (down || wheel < 0) {
                selected = std::min(selected + 1, results.size() - 1);
            }
            else if (up || wheel > 0) {
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
        poll_pack_lookup();

        if (!pending.valid() && query_delay <= 0.0f) {
            if (shown != wanted) {
                request_page(wanted);
            }
            else if ((int64_t)fetched < total && selected + PREFETCH_ROWS >= results.size()) {
                ChartQuery next = wanted;
                next.offset = (int32_t)fetched;
                request_page(next);
            }
        }

        scroll += ((float)selected * Ui::layout_px("browser.row_height") - scroll) * std::min(1.0f, elapsed_time * (float)Ui::layout("browser.scroll_speed"));
    }

    void SceneBrowser::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = (int32_t)(screen.x * Ui::layout("browser.margin_fraction"));
        int32_t y = (int32_t)(screen.y * Ui::layout("browser.title_top_fraction"));

        draw_text(skin->display_font_large, "Browse Charts", {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::layout_px("browser.title_gap");

        std::string subtitle;

        if (!error.empty()) {
            subtitle = error;
        }
        else if (!notice.empty()) {
            subtitle = notice;
        }
        else if (!shown) {
            subtitle = "Searching...";
        }
        else {
            subtitle = std::format("{} charts on {}", total, SettingsManager::settings.server_url);
        }

        draw_text(skin->display_font_small, subtitle, {x, y}, skin->color(!error.empty() ? SkinColor::TEXT_ERROR : !notice.empty() ? SkinColor::TEXT
                                                                                                                                   : SkinColor::TEXT_MUTED));
        y += Ui::layout_px("browser.subtitle_gap");

        form.draw(window, *skin, {x, y});
        result_spots.clear();
        hint_spots.clear();

        int32_t row_height = Ui::layout_px("browser.row_height");
        int32_t row_padding = Ui::layout_px("browser.row_padding");
        int32_t status_width = Ui::layout_px("browser.status_width");
        int32_t panel_x = x + TextField::width() + Ui::layout_px("browser.form_gap");
        int32_t panel_width = screen.x - panel_x - x;
        int32_t vertical_margin = (int32_t)(screen.y * Ui::layout("browser.vertical_margin_fraction"));
        int32_t top = vertical_margin;
        int32_t bottom = screen.y - vertical_margin;
        float row_gap = Ui::layout_pxf("browser.row_gap");
        uint8_t focused_highlight = (uint8_t)Ui::layout("browser.focused_highlight_alpha");
        uint8_t unfocused_highlight = (uint8_t)Ui::layout("browser.unfocused_highlight_alpha");
        uint8_t download_fill_alpha = (uint8_t)Ui::layout("browser.download_fill_alpha");
        int32_t title_top = Ui::layout_px("browser.row_title_top");
        int32_t detail_bottom = Ui::layout_px("browser.row_detail_bottom");
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
            std::optional<int64_t> pack_id = set_id ? std::nullopt : etterna_pack_id(chart);
            std::shared_ptr<BeatmapDownloads::Download> download = download_for(chart);
            BeatmapDownloads::State download_state = download != nullptr ? download->state.load() : BeatmapDownloads::State::FAILED;
            bool downloading = download != nullptr && (download_state == BeatmapDownloads::State::DOWNLOADING || download_state == BeatmapDownloads::State::EXTRACTING);
            olc::vf2d box_position = {(float)panel_x, row_top};
            olc::vf2d box_size = {(float)panel_width, (float)row_height - row_gap};
            result_spots.add({box_position, box_size}, (int32_t)index);
            uint8_t highlight = is_selected ? (results_focused ? focused_highlight : unfocused_highlight) : 0;
            olc::Pixel selection = fade(skin->color(SkinColor::BROWSER_HIGHLIGHT), highlight / 255.0);

            if (downloading) {
                float progress = download_state == BeatmapDownloads::State::EXTRACTING ? 1.0f : (float)download->progress.load();
                ProgressBar bar(box_size, skin->color(SkinColor::DOWNLOAD_TRACK));
                bar.track_color.a = std::max(bar.track_color.a, selection.a);
                olc::Pixel fill = skin->title_gradient_at(progress, 0.5);
                fill.a = download_fill_alpha;
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
            else if (pack_id) {
                status = is_selected && results_focused ? "Enter:  download from pack" : "Download from pack";
            }
            else {
                status = is_selected && results_focused ? "Enter:  download" : "Download";
            }

            std::string title = std::format("{} - {} [{}]", chart.artist, chart.title, chart.difficulty_name);
            int32_t title_baseline = row_y + title_top - title_metrics.offset.y;
            draw_text(title_font, fit_text(title_font, title, panel_width - status_width - row_padding * 3), {panel_x + row_padding, title_baseline}, color);

            std::string keys = std::format("{}K      ", chart.key_count);
            std::string difficulty = std::format("{:.2f}", chart.difficulty);
            std::string details = std::format("      {}      {} notes      {}      {}      mapped by {}", format_length(chart.duration_ms), chart.note_count, chart.status, set_id ? "osu!" : "Etterna", chart.mapper);
            int32_t detail_baseline = row_y + row_height - detail_bottom - (detail_metrics.size.y + detail_metrics.offset.y);
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

        Hint selected_action = is_downloading(selected) ? Hint{"Right-click:  cancel"} : Hint{"Enter:  download", HINT_CONFIRM};
        std::vector<Hint> hints = results_focused ? std::vector<Hint>{{"↑/↓:  chart"}, selected_action, {"Ctrl+Tab:  filters", HINT_SWITCH}, {"Esc:  back", HINT_BACK}}
                                                  : std::vector<Hint>{{"Tab, ↑/↓:  filter"}, {"←/→:  change"}, {"Enter:  next", HINT_CONFIRM}, {"Ctrl+Tab:  results", HINT_SWITCH}, {"Esc:  back", HINT_BACK}};
        int32_t hint_line_height = Ui::layout_px("browser.hint_line_height");
        int32_t hint_baseline = bottom - (detail_metrics.size.y + detail_metrics.offset.y) - (int32_t)(hints.size() - 1) * hint_line_height;

        for (const Hint& hint : hints) {
            draw_hints(window, detail_font, {hint}, {(float)x, (float)hint_baseline}, 0.0f, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), hint_spots);
            hint_baseline += hint_line_height;
        }
    }
}
