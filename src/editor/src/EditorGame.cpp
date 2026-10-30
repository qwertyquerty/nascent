#include "EditorGame.h"

#include "ArchiveExport.h"
#include "DecalDrawList.h"
#include "DifficultyCalculator.h"
#include "GameNascent.h"
#include "OsuExport.h"
#include "PatternGenerator.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "SkinSettings.h"
#include "StepManiaExport.h"
#include "Updater.h"
#include "Utf8Path.h"
#include "gse/Platform.h"
#include "gse/input/KeyEvents.h"
#include "gse/ui/Ui.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <functional>
#include <numbers>
#include <numeric>
#include <random>
#include <set>

namespace nascent {
    namespace {
        constexpr double STATUS_SECONDS = 6.0;
        constexpr double DIFFICULTY_DELAY_MS = 400.0;
        constexpr double HITSOUND_JUMP_MS = 250.0;
        constexpr size_t HITSOUNDS_PER_FRAME = 8;
        constexpr size_t GRID_LIMIT = 4000;
        constexpr int32_t SCROLL_SPEED_STEP = 10;
        constexpr size_t METRONOME_PER_FRAME = 4;
        constexpr float WAVEFORM_ROW_PX = 3.0f;
        constexpr float DENSITY_BAR_WIDTH = 44.0f;
        constexpr float DENSITY_BAR_MARGIN = 12.0f;
        constexpr float DENSITY_ROW_PX = 2.0f;
        constexpr double TIMELINE_TAIL_MS = 1000.0;
        constexpr size_t RECENT_LIMIT = 10;
        constexpr double TAP_RESET_MS = 2000.0;
        constexpr size_t TAP_LIMIT = 64;
        constexpr float JUDGE_LINE_OVERHANG = 14.0f;
        constexpr uint16_t FRAME_RATE_LIMIT = 240;
        constexpr float AUTO_SCROLL_EDGE_PX = 48.0f;
        constexpr double AUTO_SCROLL_PX_PER_SECOND = 1200.0;
        constexpr double AUTO_SCROLL_MAX_DEPTH = 2.0;
        constexpr double ON_GRID_MS = 0.5;
        constexpr const char* RECENT_FILE = "editor_recent.json";
        constexpr double AUTOPLAY_TAP_MS = 60.0;
        constexpr const char* SETTINGS_FILE = "editor_settings.json";
        constexpr double SETTINGS_SAVE_DELAY_MS = 1000.0;
        constexpr ImVec2 GAMEPLAY_PANEL_SIZE = {360.0f, 560.0f};
        constexpr const char* LIBRARY_FILE = "editor_patterns.json";
        constexpr float CHECKER_WIDTH = 620.0f;
        constexpr size_t CHECKER_TIMES = 60;
        constexpr uint32_t TICK_SAMPLE_RATE = 44100;
        constexpr double TICK_SECONDS = 0.06;
        constexpr float FONT_SIZE = 18.0f;
        const std::vector<std::string> CHART_EXTENSIONS = {".osu", ".json", ".sm", ".ssc", ".dwi"};
        const std::vector<std::string> AUDIO_EXTENSIONS = {".mp3", ".ogg", ".wav", ".flac"};
        const std::vector<std::string> IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg"};

        std::string lowercase(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return text;
        }

        bool has_extension(const std::filesystem::path& path, const std::vector<std::string>& extensions) {
            std::string extension = lowercase(path_utf8(path.extension()));
            return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
        }

        std::string clock_text(double ms) {
            int64_t total = (int64_t)std::llround(std::max(0.0, ms));
            return std::format("{}:{:02}.{:03}", total / 60000, total / 1000 % 60, total % 1000);
        }

        struct TapFit {
            double first = 0;
            double beat = 0;
            double spread = 0;
        };

        std::optional<TapFit> fit_taps(const std::vector<double>& taps) {
            size_t count = taps.size();

            if (count < 2) {
                return std::nullopt;
            }

            double mean_index = (double)(count - 1) / 2.0;
            double mean_time = std::accumulate(taps.begin(), taps.end(), 0.0) / (double)count;
            double covariance = 0;
            double variance = 0;

            for (size_t index = 0; index < count; index++) {
                covariance += ((double)index - mean_index) * (taps[index] - mean_time);
                variance += ((double)index - mean_index) * ((double)index - mean_index);
            }

            TapFit fit;
            fit.beat = covariance / variance;
            fit.first = mean_time - fit.beat * mean_index;
            double squared = 0;

            for (size_t index = 0; index < count; index++) {
                double miss = taps[index] - (fit.first + fit.beat * (double)index);
                squared += miss * miss;
            }

            fit.spread = std::sqrt(squared / (double)count);
            return fit.beat > 0 ? std::optional<TapFit>(fit) : std::nullopt;
        }

        uint8_t snap_index(uint32_t within, uint32_t divisor) {
            uint32_t denominator = within == 0 ? 1 : divisor / std::gcd(within, divisor);

            for (size_t index = 0; index < NOTE_SNAP_DIVISIONS.size(); index++) {
                if (NOTE_SNAP_DIVISIONS[index] == denominator) {
                    return (uint8_t)index;
                }
            }

            return (uint8_t)(NOTE_SNAP_COUNT - 1);
        }

        olc::Pixel faded(olc::Pixel color, uint8_t alpha) {
            color.a = alpha;
            return color;
        }

        void write_u32(std::ofstream& out, uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8) {
                out.put((char)((value >> shift) & 0xFF));
            }
        }

        void write_u16(std::ofstream& out, uint16_t value) {
            out.put((char)(value & 0xFF));
            out.put((char)((value >> 8) & 0xFF));
        }

        bool write_tick(const std::filesystem::path& path, double frequency) {
            uint32_t frames = (uint32_t)(TICK_SAMPLE_RATE * TICK_SECONDS);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);

            if (!out) {
                return false;
            }

            uint32_t data_size = frames * sizeof(int16_t);
            out.write("RIFF", 4);
            write_u32(out, 36 + data_size);
            out.write("WAVEfmt ", 8);
            write_u32(out, 16);
            write_u16(out, 1);
            write_u16(out, 1);
            write_u32(out, TICK_SAMPLE_RATE);
            write_u32(out, TICK_SAMPLE_RATE * sizeof(int16_t));
            write_u16(out, sizeof(int16_t));
            write_u16(out, 16);
            out.write("data", 4);
            write_u32(out, data_size);

            for (uint32_t frame = 0; frame < frames; frame++) {
                double t = (double)frame / TICK_SAMPLE_RATE;
                double sample = std::sin(2.0 * std::numbers::pi * frequency * t) * std::exp(-t * 70.0) * 0.8;
                write_u16(out, (uint16_t)(int16_t)std::lround(sample * 32767.0));
            }

            return (bool)out;
        }

        void outline(olc::PixelGameEngine* window, float x, float y, float w, float h, olc::Pixel color, float thickness) {
            window->FillRectDecal({x, y}, {w, thickness}, color);
            window->FillRectDecal({x, y + h - thickness}, {w, thickness}, color);
            window->FillRectDecal({x, y}, {thickness, h}, color);
            window->FillRectDecal({x + w - thickness, y}, {thickness, h}, color);
        }
    }

    EditorGame::EditorGame(olc::PixelGameEngine* window) : Game(window), imgui(window), playback(audio()) {}

    EditorGame::~EditorGame() {
        if (EditorSettings now = current_settings(); now != saved_settings) {
            save_editor_settings(settings_file(), now);
        }

        playback.unload();
        waveform.clear();
        field.reset();
        gameplay_field.reset();
        preview.reset();
        skin.reset();
    }

    void EditorGame::init() {
        const Settings& settings = SettingsManager::settings;
        settings_directory = user_config_directory(SettingsManager::APP_NAME);
        std::error_code ec;
        std::filesystem::create_directories(settings_directory, ec);

        if (!settings.audio_output_device.empty() && !audio().engine().SetPlaybackDevice(settings.audio_output_device)) {
            std::cerr << "Audio: output device \"" << settings.audio_output_device << "\" isn't available, using the system default" << std::endl;
        }

        audio().set_volumes(1.0f, 1.0f, 1.0f);
        playback.set_volume(SettingsManager::volume_gain(music_volume));
        olc::Font::init();
        Updater::check();
        pattern_seed = std::random_device{}();
        input().start();
        use_blank_canvas();
        window->GetLayers()[EDITOR_LAYER].pDrawTarget.Create(1, 1);
        window->EnableLayer(EDITOR_LAYER, true);
        gameplay_layer = (uint8_t)window->CreateLayer();
        window->GetLayers()[gameplay_layer].pDrawTarget.Create(1, 1);
        window->EnableLayer(gameplay_layer, false);
        blank_sprite = std::make_unique<olc::Sprite>(1, 1);
        blank_sprite->SetPixel(0, 0, olc::WHITE);
        blank_decal = std::make_unique<olc::Decal>(blank_sprite.get());
        Ui::set_screen(window->GetScreenSize());
        load_skin(Settings().skin);
        imgui.init(settings_directory / "editor_layout.ini", std::filesystem::path("assets") / "fonts" / "MPLUS1-Regular.ttf", FONT_SIZE);
        apply_settings(load_editor_settings(settings_file()));
        saved_settings = current_settings();
        pending_settings = saved_settings;
        allow_window_maximize();
        load_metronome();
        load_recent();
        load_library();
    }

    void EditorGame::load_metronome() {
        std::filesystem::path folder = std::filesystem::temp_directory_path() / "nascent_editor";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        constexpr std::array<double, 2> FREQUENCIES = {1760.0, 1320.0};

        for (size_t index = 0; index < metronome_sounds.size(); index++) {
            std::filesystem::path file = folder / std::format("metronome_{}.wav", index);

            if (write_tick(file, FREQUENCIES[index])) {
                metronome_sounds[index] = audio().engine().LoadSound(file.string());
            }
        }
    }

    void EditorGame::on_screen_resize() {
        Ui::set_screen(window->GetScreenSize());

        if (skin != nullptr) {
            skin->set_font_scale(Ui::scale());
        }
    }

    void EditorGame::load_skin(const std::string& id) {
        field.reset();
        gameplay_field.reset();
        skin = std::make_unique<Skin>(this, id);
        skin->set_font_scale(Ui::scale());
        Ui::set_layout_theme(skin.get());
        preview_revision = ~0ull;
    }

    void EditorGame::set_status(std::string message, bool error) {
        status = std::move(message);
        status_error = error;
        status_at = now_ms();
    }

    void EditorGame::replace_document(std::unique_ptr<ChartDocument> next) {
        open_tabs(std::move(next), {});
    }

    void EditorGame::reset_document_state() {
        selection.clear();
        selection_revision = ~0ull;
        gesture = {};
        preview_revision = ~0ull;
        difficulty.reset();
        difficulty_revision = ~0ull;
        changed_revision = ~0ull;
        document_serial++;
        hands.reset();
        findings.clear();
        findings_key = {~0ull, ~0ull, ~0ull, -1.0};
        density_revision = ~0ull;
        shown_timing_point.reset();
    }

    void EditorGame::open_tabs(std::unique_ptr<ChartDocument> main, std::vector<std::unique_ptr<ChartDocument>> others) {
        document = std::move(main);
        tabs.clear();
        active_tab = 0;

        if (document != nullptr) {
            tabs.push_back({nullptr, {}, next_tab_id++});

            for (std::unique_ptr<ChartDocument>& other : others) {
                tabs.push_back({std::move(other), {}, next_tab_id++});
            }
        }

        select_active_tab = true;
        reset_document_state();
        position = 0;
        hitsound_position = 0;
        reload_audio();
    }

    const ChartDocument* EditorGame::tab_document(size_t index) const {
        return index == active_tab ? document.get() : tabs[index].document.get();
    }

    bool EditorGame::any_dirty() const {
        return dirty_count() > 0;
    }

    size_t EditorGame::dirty_count() const {
        size_t count = 0;

        for (size_t index = 0; index < tabs.size(); index++) {
            const ChartDocument* chart = tab_document(index);
            count += chart != nullptr && chart->dirty() ? 1 : 0;
        }

        return count;
    }

    void EditorGame::switch_tab(size_t index) {
        if (document == nullptr || index >= tabs.size() || index == active_tab) {
            return;
        }

        std::filesystem::path audio = document->audio_path();
        tabs[active_tab].document = std::move(document);
        tabs[active_tab].selection = std::move(selection);
        document = std::move(tabs[index].document);
        std::vector<Hit> restored = std::move(tabs[index].selection);
        active_tab = index;
        select_active_tab = true;
        reset_document_state();
        selection = std::move(restored);

        if (document->audio_path() != audio) {
            reload_audio();
        }
    }

    void EditorGame::step_tab(int32_t direction) {
        if (tabs.size() > 1) {
            switch_tab((size_t)(((int64_t)active_tab + direction + (int64_t)tabs.size()) % (int64_t)tabs.size()));
        }
    }

    void EditorGame::close_tab() {
        if (tabs.empty()) {
            return;
        }

        std::filesystem::path audio = document != nullptr ? document->audio_path() : std::filesystem::path();
        tabs.erase(tabs.begin() + (ptrdiff_t)active_tab);
        document.reset();

        if (tabs.empty()) {
            open_tabs(nullptr, {});
            return;
        }

        active_tab = std::min(active_tab, tabs.size() - 1);
        document = std::move(tabs[active_tab].document);
        std::vector<Hit> restored = std::move(tabs[active_tab].selection);
        select_active_tab = true;
        reset_document_state();
        selection = std::move(restored);

        if (document->audio_path() != audio) {
            reload_audio();
        }
    }

    void EditorGame::save_all() {
        for (size_t index = 0; index < tabs.size(); index++) {
            ChartDocument* chart = index == active_tab ? document.get() : tabs[index].document.get();
            std::string error;

            if (chart != nullptr && chart->dirty() && !chart->path().empty()) {
                if (chart->save(error)) {
                    remember(chart->path());
                }
                else {
                    set_status(error, true);
                }
            }
        }
    }

    void EditorGame::add_difficulty() {
        std::unique_ptr<ChartDocument> next = ChartDocument::copy_of(*document, new_difficulty_keep_notes);
        std::string name = new_difficulty_name;
        uint8_t keys = (uint8_t)std::clamp(new_difficulty_keys, 1, 10);
        next->change_without_snapshot([&](ChartData& data) {
            data.info.difficulty_name = name;
            data.info.key_count = keys;
        });
        tabs.push_back({std::move(next), {}, next_tab_id++});
        switch_tab(tabs.size() - 1);
        set_status(std::format("Added {}, saving asks where to put it", name));
    }

    void EditorGame::copy_timing_to_difficulties() {
        std::vector<ChartTimingPoint> points = document->data().timing_points;
        size_t copied = 0;

        for (size_t index = 0; index < tabs.size(); index++) {
            if (index != active_tab && tabs[index].document != nullptr) {
                tabs[index].document->change([&](ChartData& data) { data.timing_points = points; });
                copied++;
            }
        }

        set_status(std::format("Copied {} timing points to {} other difficulties", points.size(), copied));
    }

    void EditorGame::reload_audio() {
        playback.unload();
        waveform.clear();

        if (document == nullptr) {
            return;
        }

        std::string error;

        if (!playback.load(document->audio_path(), error)) {
            set_status(error, true);
            return;
        }

        waveform.load(document->audio_path());
    }

    void EditorGame::open_chart(const std::filesystem::path& path) {
        std::string error;

        if (ChartDocument::is_stepmania(path)) {
            std::vector<ChartDocument::ChartChoice> choices = ChartDocument::stepmania_charts(path, error);
            size_t playable = (size_t)std::count_if(choices.begin(), choices.end(), [](const ChartDocument::ChartChoice& choice) { return choice.error.empty(); });

            if (playable == 0) {
                std::string reason = !error.empty() ? error : choices.empty() ? "no charts in this file"
                                                                              : choices.front().error;
                set_status(std::format("Couldn't open {}: {}", path_utf8(path.filename()), reason), true);
                return;
            }

            chart_choices = std::move(choices);
            choice_source = path;

            if (playable == 1) {
                auto only = std::find_if(chart_choices.begin(), chart_choices.end(), [](const ChartDocument::ChartChoice& choice) { return choice.error.empty(); });
                open_choice((size_t)(only - chart_choices.begin()));
            }
            else {
                choice_requested = true;
            }

            return;
        }

        std::unique_ptr<ChartDocument> next = ChartDocument::open(path, error);

        if (next == nullptr) {
            set_status(std::format("Couldn't open {}: {}", path_utf8(path.filename()), error), true);
            return;
        }

        std::vector<std::unique_ptr<ChartDocument>> siblings = ChartDocument::open_siblings(path, *next);
        size_t others = siblings.size();
        open_tabs(std::move(next), std::move(siblings));
        remember(path);
        std::string also = others > 0 ? std::format(" with {} more {} of the song", others, others == 1 ? "difficulty" : "difficulties") : std::string();
        set_status(document->path().empty() ? std::format("Opened {}{}, saving makes a Nascent .json copy next to it", path_utf8(path.filename()), also) : std::format("Opened {}{}", path_utf8(path.filename()), also));
    }

    void EditorGame::load_recent() {
        recent_files.clear();
        std::ifstream in(settings_directory / RECENT_FILE, std::ios::binary);

        if (!in) {
            return;
        }

        try {
            nlohmann::json data = nlohmann::json::parse(in);

            for (const nlohmann::json& entry : data.value("recent", nlohmann::json::array())) {
                if (entry.is_string() && recent_files.size() < RECENT_LIMIT) {
                    recent_files.push_back(utf8_path(entry.get<std::string>()));
                }
            }
        }
        catch (const nlohmann::json::exception&) {
            recent_files.clear();
        }
    }

    void EditorGame::save_recent() const {
        nlohmann::json list = nlohmann::json::array();

        for (const std::filesystem::path& path : recent_files) {
            list.push_back(path_utf8(path));
        }

        std::ofstream out(settings_directory / RECENT_FILE, std::ios::binary | std::ios::trunc);

        if (out) {
            out << nlohmann::json{{"recent", list}}.dump(1, ' ');
        }
    }

    void EditorGame::remember(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(path, ec);
        std::filesystem::path entry = ec ? path : absolute.lexically_normal();
        std::erase(recent_files, entry);
        recent_files.insert(recent_files.begin(), entry);

        if (recent_files.size() > RECENT_LIMIT) {
            recent_files.resize(RECENT_LIMIT);
        }

        save_recent();
    }

    void EditorGame::forget_recent(const std::filesystem::path& path) {
        std::erase(recent_files, path);
        save_recent();
    }

    std::optional<std::filesystem::path> EditorGame::draw_recent_items() {
        std::optional<std::filesystem::path> chosen;
        std::optional<std::filesystem::path> missing;

        for (const std::filesystem::path& path : recent_files) {
            std::error_code ec;
            bool exists = std::filesystem::is_regular_file(path, ec);
            std::string label = std::format("{}##{}", path_utf8(path.filename()), path_utf8(path));

            if (ImGui::MenuItem(label.c_str(), nullptr, false, true)) {
                if (exists) {
                    chosen = path;
                }
                else {
                    missing = path;
                }
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s%s", path_utf8(path).c_str(), exists ? "" : "\n(missing, click to remove it from the list)");
            }
        }

        if (missing.has_value()) {
            forget_recent(*missing);
            set_status(std::format("{} is gone, removed it from recent files", path_utf8(missing->filename())), true);
        }

        return chosen;
    }

    void EditorGame::open_choice(size_t index) {
        if (index >= chart_choices.size()) {
            return;
        }

        const ChartDocument::ChartChoice& choice = chart_choices[index];
        std::string error;
        std::unique_ptr<ChartDocument> next = ChartDocument::from_json(choice.chart, choice_source, error);

        if (next == nullptr) {
            set_status(std::format("Couldn't open {}: {}", choice.label, error), true);
            return;
        }

        std::vector<std::unique_ptr<ChartDocument>> others;

        for (size_t other = 0; other < chart_choices.size(); other++) {
            std::string ignored;

            if (other != index && chart_choices[other].error.empty()) {
                if (std::unique_ptr<ChartDocument> sibling = ChartDocument::from_json(chart_choices[other].chart, choice_source, ignored)) {
                    others.push_back(std::move(sibling));
                }
            }
        }

        open_tabs(std::move(next), std::move(others));
        remember(choice_source);
        std::string note = choice.modified ? ", some notes it can't play (like mines or rolls) were changed" : "";
        set_status(std::format("Opened {} from {}{}. Saving makes a Nascent .json copy next to it", choice.label, path_utf8(choice_source.filename()), note));
        chart_choices.clear();
    }

    void EditorGame::new_from_audio(const std::filesystem::path& audio_file) {
        replace_document(ChartDocument::from_audio(audio_file));
        set_status(std::format("New chart for {}. Set the BPM and offset in Timing before placing notes", path_utf8(audio_file.filename())));
    }

    void EditorGame::save() {
        if (document == nullptr) {
            return;
        }

        if (document->path().empty()) {
            save_as();
            return;
        }

        std::string error;

        if (document->save(error)) {
            remember(document->path());
            set_status(std::format("Saved {}", path_utf8(document->path().filename())));
        }
        else {
            set_status(error, true);
        }
    }

    void EditorGame::save_as() {
        if (document == nullptr) {
            return;
        }

        std::filesystem::path name = document->path().empty() ? document->suggested_file_name() : document->path().filename();
        dialog.open(FileDialog::Mode::SAVE, "Save chart", document->directory(), {".json"}, path_utf8(name));
        dialog_purpose = DialogPurpose::SAVE_AS;
    }

    void EditorGame::request(Pending action) {
        bool dirty = action == Pending::CLOSE_TAB ? document != nullptr && document->dirty() : any_dirty();

        if (dirty) {
            pending = action;
            confirm_requested = true;
            return;
        }

        perform(action);
    }

    void EditorGame::perform(Pending action) {
        std::filesystem::path start = document != nullptr ? document->directory() : std::filesystem::path();
        std::filesystem::path file = std::exchange(pending_file, std::filesystem::path());
        pending = Pending::NONE;

        switch (action) {
            case Pending::NEW_FROM_AUDIO:
                if (!file.empty()) {
                    new_from_audio(file);
                    return;
                }

                dialog.open(FileDialog::Mode::OPEN, "New chart from audio", start, AUDIO_EXTENSIONS);
                dialog_purpose = DialogPurpose::NEW_FROM_AUDIO;
                return;
            case Pending::OPEN_CHART:
                if (!file.empty()) {
                    open_chart(file);
                    return;
                }

                dialog.open(FileDialog::Mode::OPEN, "Open chart", start, CHART_EXTENSIONS);
                dialog_purpose = DialogPurpose::OPEN_CHART;
                return;
            case Pending::QUIT:
                quit_confirmed = true;
                quit();
                return;
            case Pending::CLOSE_TAB:
                close_tab();
                return;
            case Pending::NONE:
                return;
        }
    }

    bool EditorGame::can_close() {
        if (quit_confirmed || !any_dirty()) {
            return true;
        }

        request(Pending::QUIT);
        return false;
    }

    void EditorGame::seek(double target) {
        position = std::max(0.0, target);
        playback.seek(position);
        hitsound_position = position;
    }

    void EditorGame::toggle_playback() {
        if (!playback.is_playing()) {
            playback.seek(position);
        }

        playback.toggle();
        position = playback.position();
        hitsound_position = position;
    }

    void EditorGame::change_snap(int32_t direction) {
        if (direction > 0) {
            auto finer = std::upper_bound(DIVISORS.begin(), DIVISORS.end(), divisor);
            divisor = finer != DIVISORS.end() ? *finer : divisor;
        }
        else {
            auto coarser = std::lower_bound(DIVISORS.begin(), DIVISORS.end(), divisor);
            divisor = coarser != DIVISORS.begin() ? *(coarser - 1) : divisor;
        }
    }

    void EditorGame::change_scroll_speed(int32_t notches) {
        scroll_speed = (uint32_t)std::clamp<int32_t>((int32_t)scroll_speed + notches * SCROLL_SPEED_STEP, (int32_t)MIN_SCROLL_SPEED, (int32_t)MAX_SCROLL_SPEED);
    }

    int EditorGame::lane_width_percent() const {
        if (lane_width.has_value() || skin == nullptr) {
            return lane_width.value_or(12);
        }

        return (int)std::lround(skin_setting(*skin, skin_settings::LANE_WIDTH));
    }

    void EditorGame::change_lane_width(int32_t steps) {
        lane_width = std::clamp(lane_width_percent() + steps, MIN_LANE_WIDTH, MAX_LANE_WIDTH);
    }

    void EditorGame::step(int32_t steps) {
        if (document != nullptr) {
            seek(document->step(position, divisor, steps));
        }
    }

    double EditorGame::place_time(double time) const {
        return std::max(0.0, snap_enabled ? document->snap(time, divisor) : std::round(time));
    }

    std::string EditorGame::snap_label() const {
        return snap_enabled ? std::format("1/{}", divisor) : std::format("1/{} (no snap)", divisor);
    }

    void EditorGame::configure_field() {
        field->underlay = [this](olc::PixelGameEngine* target) {
            draw_underlay(target);
        };
        field->draw_hit_score = false;
        field->draw_combo = false;
        field->draw_notes_past_judge = true;
        field->draw_judge_colors = true;
        field->upscroll = false;
        field->mirror = false;
        field->note_gradient = skin_toggle(*skin, skin_toggles::NOTE_GRADIENT);

        field->snap_colors = snap_colors;
        if (!note_opacity.has_value()) {
            note_opacity = (float)(skin_setting(*skin, skin_settings::NOTE_OPACITY) / 100.0);
        }

        field->note_opacity = *note_opacity;
        field->receptor_opacity = (float)(skin_setting(*skin, skin_settings::RECEPTOR_OPACITY) / 100.0);
    }

    void EditorGame::update_preview() {
        if (document == nullptr) {
            field.reset();
            gameplay_field.reset();
            preview.reset();
            preview_revision = ~0ull;
            return;
        }

        if (preview_revision == document->revision() && field != nullptr) {
            return;
        }

        field.reset();
        gameplay_field.reset();
        preview = document->preview();

        if (preview == nullptr) {
            return;
        }

        SkinLayoutScope layout_scope(skin.get(), preview->info.key_count);
        field = std::make_unique<EditorField>(preview.get(), skin.get(), olc::vd2d{view.x, view.y}, olc::vd2d{std::max(1.0f, view.w), std::max(1.0f, view.h)});
        configure_field();
        preview_revision = document->revision();
    }

    void EditorGame::update_difficulty() {
        if (document == nullptr) {
            return;
        }

        if (difficulty_job.valid() && difficulty_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            DifficultyResult result = difficulty_job.get();

            if (difficulty_serial == document_serial) {
                difficulty = std::move(result);
            }
        }

        uint64_t revision = document->revision();

        if (changed_revision != revision) {
            changed_revision = revision;
            changed_at = now_ms();
        }

        if (difficulty_job.valid() || difficulty_revision == revision || now_ms() - changed_at < DIFFICULTY_DELAY_MS) {
            return;
        }

        difficulty_revision = revision;
        difficulty_serial = document_serial;
        difficulty_job = std::async(std::launch::async, [data = document->data()] {
            DifficultyResult result;
            std::unique_ptr<Chart> chart = Chart::create(data.info, data.hits, data.timing_points, result.error);

            if (chart != nullptr) {
                result.rating = DifficultyCalculator::calculate(*chart);
            }

            return result;
        });
    }

    void EditorGame::play_hitsounds(double from, double to) {
        if (document == nullptr || skin == nullptr || to <= from || to - from > HITSOUND_JUMP_MS) {
            return;
        }

        const std::vector<Hit>& hits = document->data().hits;
        auto first = std::upper_bound(hits.begin(), hits.end(), from, [](double time, const Hit& hit) { return time < hit.time; });
        size_t played = 0;
        int32_t last_time = INT32_MIN;

        for (auto it = first; it != hits.end() && it->time <= to && played < HITSOUNDS_PER_FRAME; ++it) {
            if (it->time == last_time) {
                continue;
            }

            last_time = it->time;
            int32_t voice = skin->hitsound_voice();

            if (voice >= 0) {
                play_sound(voice, hitsound_volume);
                played++;
            }
        }
    }

    void EditorGame::play_sound(int id, uint8_t volume) {
        olc::MiniAudio& engine = audio().engine();

        if (engine.IsPlaying(id)) {
            engine.Stop(id);
        }

        engine.SetVolume(id, SettingsManager::volume_gain(volume));
        engine.Play(id);
    }

    void EditorGame::play_metronome(double from, double to) {
        if (document == nullptr || document->data().timing_points.empty() || to <= from || to - from > HITSOUND_JUMP_MS) {
            return;
        }

        double beat = document->snap(from, 1);

        if (beat <= from) {
            beat = document->step(beat, 1, 1);
        }

        for (size_t played = 0; beat <= to && played < METRONOME_PER_FRAME; played++) {
            const ChartTimingPoint* point = document->timing_point_at(beat);
            int64_t index = (int64_t)std::llround((beat - point->time) / point->beat_length);
            bool accent = ((index % point->meter) + point->meter) % point->meter == 0;
            int sound = metronome_sounds[accent ? 0 : 1];

            if (sound >= 0) {
                play_sound(sound, metronome_volume);
            }

            double next = document->step(beat, 1, 1);

            if (next <= beat + 0.01) {
                break;
            }

            beat = next;
        }
    }

    void EditorGame::update(float elapsed_time) {
        limit_frame_rate(FRAME_RATE_LIMIT);
        begin_frame(elapsed_time);

        if (applied_ui_scale != ui_scale) {
            imgui.set_scale(ui_scale);
            applied_ui_scale = ui_scale;
        }

        imgui.begin_frame(input(), elapsed_time);
        handle_dropped_files();
        update_updater();

        playback.update();
        waveform.update();

        if (playback.is_playing()) {
            double now = playback.position();
            double lead = audio().engine().GetOutputLatencyMilliseconds() * playback.rate();

            if (hitsounds) {
                play_hitsounds(hitsound_position + lead, now + lead);
            }

            if (metronome) {
                play_metronome(hitsound_position + lead, now + lead);
            }

            position = now;
            hitsound_position = now;
        }

        draw_menu();
        draw_status_bar();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiID dockspace = ImGui::GetID("editor_dockspace");

        if (!layout_ready || layout_reset) {
            if (layout_reset || ImGui::DockBuilderGetNode(dockspace) == nullptr) {
                build_layout(dockspace);
            }

            layout_ready = true;
            layout_reset = false;
        }

        ImGui::DockSpaceOverViewport(dockspace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace); central != nullptr) {
            view = {central->Pos.x, central->Pos.y, central->Size.x, central->Size.y};
        }
        else {
            view = {viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y};
        }

        draw_difficulty_tabs();
        draw_transport();
        draw_chart_panel();
        draw_timing_panel();
        draw_help_panel();
        draw_timestamps_panel();
        draw_settings_window();
        draw_dialogs();

        update_preview();
        draw_gameplay_panel();
        layout_field();
        handle_field_input();
        handle_shortcuts();
        collect_grid();
        draw_density_bar();
        update_difficulty();
        update_checker();
        update_settings_file();

        imgui.end_frame();
    }

    std::filesystem::path EditorGame::settings_file() const {
        return settings_directory / SETTINGS_FILE;
    }

    EditorSettings EditorGame::current_settings() const {
        EditorSettings settings;
        settings.scroll_speed = scroll_speed;
        settings.lane_width = lane_width;
        settings.judge_offset = judge_offset;
        settings.divisor = divisor;
        settings.snap_enabled = snap_enabled;
        settings.hitsounds = hitsounds;
        settings.metronome = metronome;
        settings.snap_colors = snap_colors;
        settings.music_volume = music_volume;
        settings.hitsound_volume = hitsound_volume;
        settings.metronome_volume = metronome_volume;
        settings.waveform_opacity = waveform_opacity;
        settings.note_opacity = note_opacity;
        settings.playback_rate = playback.rate();
        settings.skin = skin != nullptr ? skin->get_id() : std::string();
        settings.gameplay_preview = gameplay_open;
        settings.timestamps_panel = timestamps_open;
        settings.ui_scale = ui_scale;
        settings.bindings = bindings;
        return settings;
    }

    void EditorGame::apply_settings(const EditorSettings& settings) {
        scroll_speed = settings.scroll_speed;
        lane_width = settings.lane_width;
        judge_offset = settings.judge_offset;
        divisor = settings.divisor;
        snap_enabled = settings.snap_enabled;
        hitsounds = settings.hitsounds;
        metronome = settings.metronome;
        snap_colors = settings.snap_colors;
        music_volume = settings.music_volume;
        hitsound_volume = settings.hitsound_volume;
        metronome_volume = settings.metronome_volume;
        waveform_opacity = settings.waveform_opacity;
        note_opacity = settings.note_opacity;
        gameplay_open = settings.gameplay_preview;
        timestamps_open = settings.timestamps_panel;
        ui_scale = std::clamp(settings.ui_scale, MIN_UI_SCALE, MAX_UI_SCALE);
        bindings = settings.bindings;
        playback.set_rate(settings.playback_rate);
        playback.set_volume(SettingsManager::volume_gain(music_volume));
        preview_revision = ~0ull;

        if (!settings.skin.empty() && (skin == nullptr || skin->get_id() != settings.skin)) {
            for (const SkinInfo& info : Skin::available_skins()) {
                if (info.id == settings.skin) {
                    load_skin(info.id);
                    break;
                }
            }
        }
    }

    void EditorGame::update_settings_file() {
        EditorSettings now = current_settings();

        if (now != pending_settings) {
            pending_settings = std::move(now);
            settings_changed_at = now_ms();
            return;
        }

        if (pending_settings != saved_settings && now_ms() - settings_changed_at >= SETTINGS_SAVE_DELAY_MS) {
            if (save_editor_settings(settings_file(), pending_settings)) {
                saved_settings = pending_settings;
            }
            else {
                settings_changed_at = now_ms();
            }
        }
    }

    std::string EditorGame::shortcut(EditorAction action) const {
        return bindings.label(action);
    }

    void EditorGame::draw_settings_window() {
        if (!settings_open) {
            capturing.reset();
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(ui_px(640), ui_px(620)), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Settings", &settings_open, ImGuiWindowFlags_NoDocking)) {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("##settings_tabs")) {
            if (ImGui::BeginTabItem("Key bindings")) {
                ImGui::TextWrapped("Click a binding, then press the key for it. Hold Ctrl, Shift or Alt with it for a combination. Esc cancels and right-click clears a binding. A key that's already used moves to the new action.");

                if (ImGui::Button("Reset all bindings")) {
                    bindings.reset();
                    capturing.reset();
                }

                ImGui::Spacing();

                if (ImGui::BeginTable("##bindings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 2.2f);
                    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                    ImGui::TableSetupColumn("Other key", ImGuiTableColumnFlags_WidthStretch, 1.2f);
                    ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableHeadersRow();
                    std::string_view group;

                    for (size_t index = 0; index < EDITOR_ACTION_COUNT; index++) {
                        EditorAction action = (EditorAction)index;
                        const ActionInfo& info = action_info(action);
                        ImGui::PushID((int)index);

                        if (group != info.group) {
                            group = info.group;
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextDisabled("%s", info.group);
                        }

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(info.label);

                        for (size_t slot = 0; slot < CHORDS_PER_ACTION; slot++) {
                            ImGui::TableNextColumn();
                            bool waiting = capturing == std::optional<std::pair<EditorAction, size_t>>({action, slot});
                            std::string text = waiting ? "Press a key..." : chord_text(bindings.get(action, slot));
                            ImGui::PushID((int)slot);

                            if (ImGui::Button(text.empty() ? "-" : text.c_str(), ImVec2(-1, 0))) {
                                capturing = std::pair<EditorAction, size_t>{action, slot};
                            }

                            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                                bindings.set(action, slot, KeyChord{});
                                capturing.reset();
                            }

                            ImGui::PopID();
                        }

                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(bindings.is_default(action));

                        if (ImGui::SmallButton("Reset")) {
                            bindings.reset(action);
                        }

                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Editor")) {
                int percent = (int)std::lround(ui_scale * 100.0f);
                ImGui::SetNextItemWidth(ui_px(260));
                ImGui::SliderInt("Interface size", &percent, (int)std::lround(MIN_UI_SCALE * 100.0f), (int)std::lround(MAX_UI_SCALE * 100.0f), "%d%%");

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    ui_scale = std::round(percent / 5.0f) * 5.0f / 100.0f;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Size of the menus, panels and text. Also under View > Interface size.");
                }

                ImGui::Spacing();
                ImGui::TextWrapped("The scroll speed, lane width, snap, judge line, volumes, opacities, playback rate, skin, interface size and which panels are open are remembered between sessions, along with your key bindings.");
                ImGui::Spacing();

                if (ImGui::Button("Reset editor options")) {
                    EditorSettings defaults;
                    defaults.bindings = bindings;
                    defaults.skin = Settings().skin;
                    apply_settings(defaults);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Puts every option back to its default, the key bindings stay as they are");
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
        capture_binding();
    }

    void EditorGame::capture_binding() {
        if (!capturing) {
            return;
        }

        Input& keys = input();

        for (size_t value = 1; value < (size_t)olc::Key::ENUM_END; value++) {
            olc::Key key = (olc::Key)value;

            if (key == olc::Key::CTRL || key == olc::Key::SHIFT || !keys.key_pressed(key)) {
                continue;
            }

            if (key == olc::Key::ESCAPE) {
                capturing.reset();
                return;
            }

            KeyChord chord{key, keys.key_held(olc::Key::CTRL), keys.key_held(olc::Key::SHIFT), alt_held()};
            auto [action, slot] = *capturing;

            if (std::optional<EditorAction> owner = bindings.owner(chord, action)) {
                for (size_t other = 0; other < CHORDS_PER_ACTION; other++) {
                    if (bindings.get(*owner, other) == chord) {
                        bindings.set(*owner, other, KeyChord{});
                    }
                }

                set_status(std::format("{} no longer does \"{}\"", chord_text(chord), action_info(*owner).label));
            }

            bindings.set(action, slot, chord);
            capturing.reset();
            return;
        }
    }

    void EditorGame::draw_difficulty_tabs() {
        if (document == nullptr) {
            return;
        }

        float height = ImGui::GetFrameHeight() + 8.0f;
        ImGui::SetNextWindowPos(ImVec2(view.x, view.y));
        ImGui::SetNextWindowSize(ImVec2(view.w, height));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui_px(6.0f), ui_px(4.0f)));

        if (ImGui::Begin("##difficulties", nullptr, flags)) {
            std::optional<size_t> clicked;
            std::optional<size_t> closing;

            if (ImGui::BeginTabBar("##difficulty_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                for (size_t index = 0; index < tabs.size(); index++) {
                    const ChartDocument* chart = tab_document(index);
                    const ChartInfo& info = chart->data().info;
                    std::string label = std::format("{}###difficulty{}", info.difficulty_name.empty() ? "Unnamed" : info.difficulty_name, tabs[index].id);
                    ImGuiTabItemFlags item_flags = chart->dirty() ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None;
                    bool open = true;

                    if (index == active_tab && select_active_tab) {
                        item_flags |= ImGuiTabItemFlags_SetSelected;
                    }

                    if (ImGui::BeginTabItem(label.c_str(), &open, item_flags)) {
                        if (index != active_tab && !select_active_tab) {
                            clicked = index;
                        }

                        ImGui::EndTabItem();
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", std::format("{}K, {} notes{}", info.key_count, chart->data().hits.size(), chart->path().empty() ? ", not saved yet" : "").c_str());
                    }

                    if (!open) {
                        closing = index;
                    }
                }

                if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip)) {
                    new_difficulty_requested = true;
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("New difficulty for this song");
                }

                ImGui::EndTabBar();
            }

            select_active_tab = false;

            if (closing) {
                switch_tab(*closing);
                request(Pending::CLOSE_TAB);
            }
            else if (clicked) {
                switch_tab(*clicked);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar();
        view.y += height;
        view.h = std::max(0.0f, view.h - height);
    }

    void EditorGame::draw_new_difficulty_popup() {
        if (new_difficulty_requested && document != nullptr) {
            ImGui::OpenPopup("New difficulty");
            new_difficulty_name.clear();
            new_difficulty_keys = document->data().info.key_count;
            new_difficulty_keep_notes = false;
        }

        new_difficulty_requested = false;

        if (!ImGui::BeginPopupModal("New difficulty", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        if (document == nullptr) {
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }

        ImGui::TextDisabled("It starts with this difficulty's song, metadata and timing.");
        ImGui::Spacing();

        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(ui_px(260));
        bool entered = ImGui::InputText("Name", &new_difficulty_name, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SetNextItemWidth(ui_px(260));
        ImGui::SliderInt("Keys", &new_difficulty_keys, 1, 10);
        ImGui::Checkbox("Copy the notes from this difficulty", &new_difficulty_keep_notes);

        if (new_difficulty_keep_notes && new_difficulty_keys < document->data().info.key_count) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Notes past lane %d are left out.", new_difficulty_keys);
        }

        ImGui::Spacing();
        bool named = !new_difficulty_name.empty();
        ImGui::BeginDisabled(!named);

        if (ImGui::Button("Create", ImVec2(ui_px(110), 0)) || (entered && named)) {
            ImGui::CloseCurrentPopup();
            add_difficulty();
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorGame::draw_inverse_popup() {
        if (inverse_requested) {
            ImGui::OpenPopup("Inverse selection");
            inverse_requested = false;
        }

        if (!ImGui::BeginPopupModal("Inverse selection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        prune_selection();

        if (document == nullptr || selection.empty()) {
            ImGui::TextUnformatted("Nothing is selected anymore.");
        }
        else {
            ImGui::TextUnformatted("Each selected note becomes a long note that lasts until the next note in its lane,");
            ImGui::TextUnformatted("leaving a gap before it.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(ui_px(120));
            snap_picker("Gap", inverse_divisor);

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("How much space is left before the next note, as a part of a beat. Notes closer together than twice the gap get long notes half as long as the space.");
            }

            std::vector<Hit> result = document->inverted(selection, inverse_divisor);
            size_t changed = 0;

            for (size_t index = 0; index < selection.size(); index++) {
                changed += !same_hit(result[index], selection[index]) ? 1 : 0;
            }

            ImGui::Spacing();
            ImGui::TextUnformatted(std::format("{} of {} selected notes change.", changed, selection.size()).c_str());

            if (changed < selection.size()) {
                ImGui::TextDisabled("The rest are the last note in their lane, or already exactly right.");
            }

            ImGui::Spacing();
            ImGui::BeginDisabled(changed == 0);

            if (ImGui::Button("Inverse", ImVec2(ui_px(110), 0))) {
                document->change([&](ChartData& data) { replace_hits(data, selection, result); });
                selection.clear();

                for (const Hit& hit : result) {
                    if (document->contains(hit) && !is_selected(hit)) {
                        selection.push_back(hit);
                    }
                }

                selection_revision = document->revision();
                set_status(std::format("Turned {} notes into long notes with a 1/{} gap", changed, inverse_divisor));
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndDisabled();
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorGame::copy_timestamp() {
        std::string text = format_timestamp((int32_t)std::lround(position), selection);

        if (copy_to_clipboard(text)) {
            set_status(std::format("Copied {}", text));
        }
        else {
            set_status("Couldn't copy to the clipboard", true);
        }
    }

    void EditorGame::go_to_timestamp(const Timestamp& timestamp) {
        if (document == nullptr) {
            return;
        }

        seek(timestamp.time);
        selection.clear();

        for (const auto& [time, lane] : timestamp.notes) {
            auto found = std::find_if(document->data().hits.begin(), document->data().hits.end(), [&](const Hit& hit) { return hit.time == time && hit.key == lane; });

            if (found != document->data().hits.end() && !is_selected(*found)) {
                selection.push_back(*found);
            }
        }

        selection_revision = document->revision();

        if (timestamp.notes.empty()) {
            set_status(std::format("Went to {}", format_clock(timestamp.time)));
        }
        else {
            set_status(std::format("Went to {} and selected {} of its {} notes", format_clock(timestamp.time), selection.size(), timestamp.notes.size()), selection.size() < timestamp.notes.size());
        }
    }

    void EditorGame::go_to_clipboard_timestamp() {
        std::vector<Timestamp> found = find_timestamps(read_clipboard().value_or(std::string()));

        if (!found.empty()) {
            go_to_timestamp(found.front());
            return;
        }

        goto_text.clear();
        goto_requested = true;
    }

    void EditorGame::draw_goto_popup() {
        if (goto_requested) {
            ImGui::OpenPopup("Go to timestamp");
            goto_requested = false;
        }

        if (!ImGui::BeginPopupModal("Go to timestamp", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(ui_px(360));
        bool entered = ImGui::InputTextWithHint("##timestamp", "00:12:345 (12345|0,12400|1)", &goto_text, ImGuiInputTextFlags_EnterReturnsTrue);
        std::vector<Timestamp> found = find_timestamps(goto_text);

        if (found.empty()) {
            ImGui::TextDisabled("Type or paste a timestamp, like ones copied with %s", shortcut(EditorAction::COPY_TIMESTAMP).c_str());
        }
        else {
            ImGui::TextDisabled("%s", std::format("{}{}", format_clock(found.front().time), found.front().notes.empty() ? std::string() : std::format(", {} notes", found.front().notes.size())).c_str());
        }

        ImGui::BeginDisabled(found.empty());

        if (ImGui::Button("Go", ImVec2(ui_px(110), 0)) || (entered && !found.empty())) {
            ImGui::CloseCurrentPopup();
            go_to_timestamp(found.front());
        }

        ImGui::EndDisabled();
        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorGame::draw_timestamps_panel() {
        if (!timestamps_open) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(ui_px(420), ui_px(420)), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Timestamps", &timestamps_open)) {
            ImGui::End();
            return;
        }

        ImGui::TextWrapped("Paste feedback or mod notes with timestamps in them, then click a timestamp to jump there with its notes selected.");

        if (ImGui::Button("Paste")) {
            if (std::optional<std::string> text = read_clipboard()) {
                timestamp_notes += (timestamp_notes.empty() || timestamp_notes.ends_with('\n') ? "" : "\n") + *text;
            }
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear")) {
            timestamp_notes.clear();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(document == nullptr);

        if (ImGui::Button("Copy a timestamp")) {
            copy_timestamp();
        }

        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Copies the playhead and the selected notes (%s)", shortcut(EditorAction::COPY_TIMESTAMP).c_str());
        }

        ImGui::InputTextMultiline("##notes", &timestamp_notes, ImVec2(-1, ImGui::GetTextLineHeight() * 6));
        ImGui::Separator();
        size_t line_start = 0;
        int id = 0;

        while (line_start <= timestamp_notes.size()) {
            size_t line_end = timestamp_notes.find('\n', line_start);
            std::string_view line = std::string_view(timestamp_notes).substr(line_start, line_end == std::string::npos ? std::string::npos : line_end - line_start);
            std::vector<Timestamp> found = find_timestamps(line);

            if (!found.empty()) {
                std::string rest;
                size_t cursor = 0;

                for (const Timestamp& timestamp : found) {
                    ImGui::PushID(id++);
                    ImGui::BeginDisabled(document == nullptr);

                    if (ImGui::SmallButton(std::string(line.substr(timestamp.start, timestamp.length)).c_str())) {
                        go_to_timestamp(timestamp);
                    }

                    ImGui::EndDisabled();
                    ImGui::PopID();
                    ImGui::SameLine();
                    rest += line.substr(cursor, timestamp.start - cursor);
                    cursor = timestamp.start + timestamp.length;
                }

                rest += line.substr(std::min(cursor, line.size()));
                size_t text_start = rest.find_first_not_of(" -\t");
                ImGui::TextWrapped("%s", text_start == std::string::npos ? "" : rest.c_str() + text_start);
            }

            if (line_end == std::string::npos) {
                break;
            }

            line_start = line_end + 1;
        }

        ImGui::End();
    }

    void EditorGame::start_test_play() {
        if (document == nullptr) {
            return;
        }

        int32_t from = (int32_t)std::lround(position);
        ChartData data = document->data();
        data.info.audio_file = path_utf8(document->audio_path());

        if (!data.info.image_file.empty()) {
            std::filesystem::path image = utf8_path(data.info.image_file);
            data.info.image_file = path_utf8(image.is_absolute() ? image : document->directory() / image);
        }

        std::erase_if(data.hits, [&](const Hit& hit) { return hit.time < from; });
        std::string error;

        if (data.hits.empty()) {
            set_status("There are no notes after the playhead to play", true);
            return;
        }

        if (Chart::create(data.info, data.hits, data.timing_points, error) == nullptr) {
            set_status(std::format("Can't test play yet: {}", error), true);
            return;
        }

        std::filesystem::path request = GameNascent::test_play_file();
        std::error_code ec;
        std::filesystem::create_directories(request.parent_path(), ec);

        {
            std::ofstream out(request, std::ios::binary | std::ios::trunc);
            out << nlohmann::json{{"chart", nlohmann::json::parse(chart_to_json(data))}, {"start", from}, {"rate", playback.rate()}}.dump();

            if (!out) {
                set_status("Couldn't write the test play file", true);
                return;
            }
        }

        if (playback.is_playing()) {
            toggle_playback();
        }

        if (!spawn_detached(executable_path(), {std::string(GameNascent::TEST_PLAY_ARGUMENT)})) {
            set_status("Couldn't start the test play", true);
            return;
        }

        set_status(std::format("Test playing from {}{}, Esc closes it", format_clock(from), playback.rate() != 1.0f ? std::format(" at {:g}x", playback.rate()) : std::string()));
    }

    void EditorGame::build_layout(unsigned int dockspace) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

        ImGuiID center = dockspace;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
        ImGuiID gameplay = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.40f, nullptr, &center);
        ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.40f, nullptr, &left);
        ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, nullptr, &right);

        ImGui::DockBuilderDockWindow("Chart", left);
        ImGui::DockBuilderDockWindow("Timing", left_bottom);
        ImGui::DockBuilderDockWindow("Transport", right);
        ImGui::DockBuilderDockWindow("Help", right_bottom);
        ImGui::DockBuilderDockWindow("Gameplay preview", gameplay);
        ImGui::DockBuilderFinish(dockspace);
    }

    void EditorGame::draw_menu() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        bool open = document != nullptr;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New from audio...", shortcut(EditorAction::NEW_FROM_AUDIO).c_str())) {
                request(Pending::NEW_FROM_AUDIO);
            }

            if (ImGui::MenuItem("Open...", shortcut(EditorAction::OPEN_CHART).c_str())) {
                request(Pending::OPEN_CHART);
            }

            if (ImGui::BeginMenu("Open recent", !recent_files.empty())) {
                if (std::optional<std::filesystem::path> chosen = draw_recent_items()) {
                    pending_file = *chosen;
                    request(Pending::OPEN_CHART);
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Clear recent")) {
                    recent_files.clear();
                    save_recent();
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("New difficulty...", nullptr, false, open)) {
                new_difficulty_requested = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save", shortcut(EditorAction::SAVE).c_str(), false, open)) {
                save();
            }

            if (ImGui::MenuItem("Save as...", shortcut(EditorAction::SAVE_AS).c_str(), false, open)) {
                save_as();
            }

            if (tabs.size() > 1 && ImGui::MenuItem("Save all difficulties", nullptr, false, any_dirty())) {
                save_all();

                for (size_t index = 0; index < tabs.size(); index++) {
                    if (const ChartDocument* chart = tab_document(index); chart != nullptr && chart->dirty()) {
                        switch_tab(index);
                        save_as();
                        break;
                    }
                }
            }

            if (ImGui::BeginMenu("Export", open)) {
                if (ImGui::MenuItem("osu! chart (.osu)...")) {
                    osu_prompt_purpose = DialogPurpose::EXPORT_OSU;
                    osu_prompt_requested = true;
                }

                if (ImGui::MenuItem("osu! archive with audio (.osz)...")) {
                    osu_prompt_purpose = DialogPurpose::EXPORT_OSZ;
                    osu_prompt_requested = true;
                }

                if (ImGui::MenuItem("Nascent archive with audio (.zip)...")) {
                    open_export(DialogPurpose::EXPORT_ZIP, "Export .zip", ".zip");
                }

                ImGui::Separator();

                if (ImGui::MenuItem("StepMania (.sm)...")) {
                    open_export(DialogPurpose::EXPORT_STEPMANIA, "Export .sm", ".sm");
                }

                if (ImGui::MenuItem("StepMania 5 (.ssc)...")) {
                    open_export(DialogPurpose::EXPORT_STEPMANIA, "Export .ssc", ".ssc");
                }

                if (ImGui::MenuItem("Dance With Intensity (.dwi)...")) {
                    open_export(DialogPurpose::EXPORT_STEPMANIA, "Export .dwi", ".dwi");
                }

                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Test play from the playhead", shortcut(EditorAction::TEST_PLAY).c_str(), false, open)) {
                start_test_play();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Plays the chart in the game from the playhead, with your game settings. Nothing is saved or submitted.");
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Settings...", shortcut(EditorAction::SETTINGS).c_str())) {
                settings_open = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Quit", shortcut(EditorAction::QUIT).c_str())) {
                request(Pending::QUIT);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", shortcut(EditorAction::UNDO).c_str(), false, open && document->can_undo())) {
                document->undo();
            }

            if (ImGui::MenuItem("Redo", shortcut(EditorAction::REDO).c_str(), false, open && document->can_redo())) {
                document->redo();
            }

            ImGui::Separator();

            bool any = open && !selection.empty();

            if (ImGui::MenuItem("Select all", shortcut(EditorAction::SELECT_ALL).c_str(), false, open)) {
                select_all();
            }

            if (ImGui::MenuItem("Cut", shortcut(EditorAction::CUT).c_str(), false, any)) {
                cut_selection();
            }

            if (ImGui::MenuItem("Copy", shortcut(EditorAction::COPY).c_str(), false, any)) {
                copy_selection();
            }

            if (ImGui::MenuItem("Paste at playhead", shortcut(EditorAction::PASTE).c_str(), false, open && !clipboard.empty())) {
                paste_clipboard();
            }

            if (ImGui::MenuItem("Mirror selected notes", shortcut(EditorAction::MIRROR).c_str(), false, any)) {
                mirror_selection();
            }

            if (ImGui::MenuItem("Delete selected notes", shortcut(EditorAction::DELETE_SELECTION).c_str(), false, any)) {
                delete_selection();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Copy timestamp", shortcut(EditorAction::COPY_TIMESTAMP).c_str(), false, open)) {
                copy_timestamp();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Copies the playhead and the selected notes as an osu!-style timestamp, like 00:12:345 (12345|0,12400|1)");
            }

            if (ImGui::MenuItem("Go to timestamp...", shortcut(EditorAction::GO_TO_TIMESTAMP).c_str(), false, open)) {
                go_to_clipboard_timestamp();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Jumps to a timestamp on the clipboard and selects its notes, or asks for one");
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save selection to pattern library...", nullptr, false, any)) {
                open_library(true);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Transform selection...", shortcut(EditorAction::TRANSFORM).c_str(), false, open && !selection.empty())) {
                transform_requested = true;
            }

            if (ImGui::MenuItem("Inverse selection...", shortcut(EditorAction::INVERSE).c_str(), false, open && !selection.empty())) {
                inverse_requested = true;
            }

            if (ImGui::MenuItem("Generate pattern...", nullptr, false, open)) {
                pattern_divisor = divisor;
                pattern_open = true;
            }

            if (ImGui::MenuItem("Pattern library...")) {
                open_library(false);
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Check chart...", nullptr, false, open)) {
                checker_open = true;
                ImGui::SetWindowFocus("Check chart");
            }

            if (ImGui::MenuItem("Quantize selection...", nullptr, false, open && !selection.empty())) {
                quantize_divisor = divisor;
                quantize_requested = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Skin")) {
                for (const SkinInfo& info : Skin::available_skins()) {
                    if (ImGui::MenuItem(std::format("{}##{}", info.name, info.id).c_str(), nullptr, skin != nullptr && skin->get_id() == info.id)) {
                        load_skin(info.id);
                    }
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Interface size")) {
                for (int percent : {75, 100, 125, 150, 175, 200, 250}) {
                    if (ImGui::MenuItem(std::format("{}%", percent).c_str(), nullptr, std::lround(ui_scale * 100.0f) == percent)) {
                        ui_scale = percent / 100.0f;
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Other...")) {
                    settings_open = true;
                }

                ImGui::EndMenu();
            }

            ImGui::MenuItem("Gameplay preview", nullptr, &gameplay_open);
            ImGui::MenuItem("Timestamps", nullptr, &timestamps_open);
            ImGui::MenuItem("Hitsounds", nullptr, &hitsounds);
            ImGui::MenuItem("Metronome", nullptr, &metronome);

            if (ImGui::MenuItem("Snap colors", nullptr, &snap_colors) && field != nullptr) {
                field->snap_colors = snap_colors;
            }

            if (ImGui::MenuItem("Reset panel layout")) {
                layout_reset = true;
            }

            ImGui::EndMenu();
        }

        draw_update_menu();
        ImGui::EndMainMenuBar();
    }

    void EditorGame::update_updater() {
        if (std::optional<UpdateNotice> notice = Updater::take_notice()) {
            set_status(notice->message, notice->is_error);
        }

        if (Updater::ready() && !update_restart_requested) {
            update_restart_requested = true;
            set_status("Update downloaded, restarting the editor to finish it");
            request(Pending::QUIT);
        }
    }

    void EditorGame::draw_update_menu() {
        UpdateSnapshot update = Updater::snapshot();
        bool available = update.release.has_value() && (update.status == UpdateStatus::AVAILABLE || update.status == UpdateStatus::FAILED);

        if (!ImGui::BeginMenu(available ? "Update (new)###update_menu" : "Update###update_menu")) {
            return;
        }

        std::string tag = update.release.has_value() ? update.release->tag : std::string();

        switch (update.status) {
            case UpdateStatus::AVAILABLE:
            case UpdateStatus::FAILED:
                if (ImGui::MenuItem("Update")) {
                    Updater::start();
                    set_status(update.installable ? std::format("Downloading Nascent {}", tag) : "Opened the download page in your browser");
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", update.installable ? "Downloads the update, then restarts the editor (asks first if you have unsaved changes)" : "This folder can't be written to, so this opens the download page instead");
                }

                ImGui::TextDisabled("Nascent %s is available", tag.c_str());

                if (update.status == UpdateStatus::FAILED) {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "Last try failed: %s", update.error.c_str());
                }

                break;
            case UpdateStatus::DOWNLOADING:
                ImGui::MenuItem("Update", nullptr, false, false);
                ImGui::TextDisabled("Downloading Nascent %s... %d%%", tag.c_str(), (int)std::floor(update.progress * 100.0));
                break;
            case UpdateStatus::READY:
                if (ImGui::MenuItem("Restart to finish updating")) {
                    request(Pending::QUIT);
                }

                break;
            case UpdateStatus::CHECKING:
                ImGui::MenuItem("Update", nullptr, false, false);
                ImGui::TextDisabled("Checking for updates...");
                break;
            case UpdateStatus::UP_TO_DATE:
                ImGui::MenuItem("Update", nullptr, false, false);
                ImGui::TextDisabled("You're on the newest version");
                break;
            case UpdateStatus::DISABLED:
                ImGui::MenuItem("Update", nullptr, false, false);
                ImGui::TextDisabled("Updates only work in release builds");
                break;
        }

        ImGui::EndMenu();
    }

    void EditorGame::draw_status_bar() {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;

        if (ImGui::BeginViewportSideBar("##status", viewport, ImGuiDir_Down, ImGui::GetFrameHeight(), flags)) {
            if (ImGui::BeginMenuBar()) {
                if (document != nullptr) {
                    std::string name = document->path().empty() ? "unsaved chart" : path_utf8(document->path().filename());
                    ImGui::TextUnformatted(std::format("{}{}", name, document->dirty() ? " *" : "").c_str());
                    ImGui::Separator();
                    ImGui::TextUnformatted(std::format("{}  {}  {} notes{}", clock_text(position), snap_label(), document->data().hits.size(), selection.empty() ? std::string() : std::format(", {} selected", selection.size())).c_str());
                    ImGui::Separator();
                }

                if (!status.empty() && now_ms() - status_at < STATUS_SECONDS * 1000.0) {
                    ImGui::TextColored(status_error ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.75f, 0.9f, 1.0f, 1.0f), "%s", status.c_str());
                }

                ImGui::EndMenuBar();
            }
        }

        ImGui::End();
    }

    void EditorGame::draw_transport() {
        if (!ImGui::Begin("Transport")) {
            ImGui::End();
            return;
        }

        if (document == nullptr) {
            ImGui::TextDisabled("No chart open");
            ImGui::End();
            return;
        }

        if (playback.loaded()) {
            if (ImGui::Button(playback.is_playing() ? "Pause" : "Play", ImVec2(ui_px(90), 0))) {
                toggle_playback();
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(std::format("{} / {}", clock_text(position), clock_text(playback.length())).c_str());

            float value = (float)position;
            ImGui::SetNextItemWidth(-1);

            if (ImGui::SliderFloat("##seek", &value, 0.0f, (float)std::max(1.0, playback.length()), "")) {
                seek(value);
            }
        }
        else {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "No audio: choose one in Chart");
            ImGui::TextUnformatted(clock_text(position).c_str());
        }

        ImGui::SeparatorText("Playback");
        float rate = playback.rate();

        if (ImGui::BeginCombo("Rate", std::format("{:g}x", rate).c_str())) {
            for (float option : RATES) {
                if (ImGui::Selectable(std::format("{:g}x", option).c_str(), option == rate)) {
                    playback.set_rate(option);
                }
            }

            ImGui::EndCombo();
        }

        ImGui::Checkbox("Hitsounds", &hitsounds);
        ImGui::SameLine();
        ImGui::Checkbox("Metronome", &metronome);
        ImGui::SameLine();

        if (ImGui::Checkbox("Snap colors", &snap_colors) && field != nullptr) {
            field->snap_colors = snap_colors;
        }

        int opacity = (int)std::lround(waveform_opacity * 100.0f);

        if (ImGui::SliderInt("Waveform", &opacity, 0, 100, "%d%%")) {
            waveform_opacity = opacity / 100.0f;
        }

        int notes = (int)std::lround(note_opacity.value_or(1.0f) * 100.0f);

        if (ImGui::SliderInt("Note/LN opacity", &notes, 0, 100, "%d%%")) {
            note_opacity = notes / 100.0f;

            if (field != nullptr) {
                field->note_opacity = *note_opacity;
            }
        }

        if (waveform.loading()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(reading audio)");
        }

        if (const ChartTimingPoint* point = document->timing_point_at(position)) {
            ImGui::TextUnformatted(std::format("{:.2f} BPM, {}/4", 60000.0 / point->beat_length, point->meter).c_str());
        }

        ImGui::SeparatorText("Volume");

        auto volume_slider = [](const char* label, uint8_t& volume) {
            int value = volume;
            bool changed = ImGui::SliderInt(label, &value, 0, 100, "%d%%");
            volume = (uint8_t)value;
            return changed;
        };

        if (volume_slider("Music##volume", music_volume)) {
            playback.set_volume(SettingsManager::volume_gain(music_volume));
        }

        volume_slider("Hitsound##volume", hitsound_volume);
        volume_slider("Metronome##volume", metronome_volume);

        ImGui::SeparatorText("Editing");

        if (ImGui::BeginCombo("Snap", std::format("1/{}", divisor).c_str(), ImGuiComboFlags_HeightLarge)) {
            for (uint32_t option : DIVISORS) {
                if (ImGui::Selectable(std::format("1/{}", option).c_str(), option == divisor)) {
                    divisor = option;
                }
            }

            ImGui::SeparatorText("Complex tuplets");

            for (uint32_t option : COMPLEX_TUPLET_DIVISORS) {
                if (ImGui::Selectable(std::format("1/{}", option).c_str(), option == divisor)) {
                    divisor = option;
                }
            }

            ImGui::SeparatorText("Other");
            ImGui::SetNextItemWidth(ui_px(90));
            ImGui::InputInt("##custom_snap", &custom_divisor, 1, 0);
            custom_divisor = std::clamp(custom_divisor, 1, MAX_DIVISOR);
            ImGui::SameLine();

            if (ImGui::Button(std::format("Use 1/{}", custom_divisor).c_str())) {
                divisor = (uint32_t)custom_divisor;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndCombo();
        }

        ImGui::Checkbox("Snap notes", &snap_enabled);

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Off: notes go exactly where you click, to the millisecond. The grid lines stay either way. (N)");
        }

        int speed = (int)scroll_speed;

        if (ImGui::SliderInt("Scroll speed", &speed, (int)MIN_SCROLL_SPEED, (int)MAX_SCROLL_SPEED)) {
            scroll_speed = (uint32_t)speed;
        }

        int width = lane_width_percent();

        if (ImGui::SliderInt("Lane width", &width, MIN_LANE_WIDTH, MAX_LANE_WIDTH, "%d%%")) {
            lane_width = width;
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Width of each lane as a share of the window height, starts at the skin's width (Ctrl + Shift + wheel or arrows)");
        }

        ImGui::SliderFloat("Judge line", &judge_offset, 0.05f, 0.6f, "%.2f");
        ImGui::End();
    }

    void EditorGame::draw_chart_panel() {
        if (!ImGui::Begin("Chart")) {
            ImGui::End();
            return;
        }

        if (document == nullptr) {
            ImGui::TextDisabled("No chart open");
            ImGui::End();
            return;
        }

        const ChartData& data = document->data();
        const ChartInfo& info = data.info;

        auto text_field = [&](const char* label, std::string ChartInfo::* member) {
            std::string value = info.*member;
            ImGui::InputText(label, &value);

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                document->change([&](ChartData& edited) { edited.info.*member = value; });
            }
        };

        auto int_field = [&](const char* label, int current, int minimum, int maximum, const std::function<void(ChartInfo&, int)>& apply) {
            bool slider = maximum - minimum <= 40;
            std::string widget = slider ? std::string(label) : std::format("##{}", label);
            ImGuiID id = ImGui::GetID(widget.c_str());
            auto pending = editing_ints.find(id);
            int value = pending != editing_ints.end() ? pending->second : current;
            auto commit = [&](int next) {
                next = std::clamp(next, minimum, maximum);

                if (next != current) {
                    document->change([&](ChartData& edited) { apply(edited.info, next); });
                }
            };

            if (slider) {
                ImGui::SliderInt(widget.c_str(), &value, minimum, maximum);
            }
            else {
                float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                float button = ImGui::GetFrameHeight();
                ImGui::SetNextItemWidth(std::max(40.0f, ImGui::CalcItemWidth() - (button + spacing) * 2));
                ImGui::InputInt(widget.c_str(), &value, 0, 0);
            }

            if (ImGui::IsItemActive()) {
                editing_ints[id] = value;
            }
            else {
                editing_ints.erase(id);
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                commit(value);
            }

            if (!slider) {
                float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                float button = ImGui::GetFrameHeight();
                int step = ImGui::GetIO().KeyCtrl ? 100 : 10;
                ImGui::SameLine(0, spacing);

                if (ImGui::Button(std::format("-##{}", label).c_str(), ImVec2(button, button))) {
                    commit(current - step);
                }

                ImGui::SameLine(0, spacing);

                if (ImGui::Button(std::format("+##{}", label).c_str(), ImVec2(button, button))) {
                    commit(current + step);
                }

                ImGui::SameLine(0, spacing);
                ImGui::TextUnformatted(label);
            }
        };

        if (ImGui::CollapsingHeader("Metadata", ImGuiTreeNodeFlags_DefaultOpen)) {
            text_field("Title", &ChartInfo::title);
            text_field("Title (unicode)", &ChartInfo::title_unicode);
            text_field("Artist", &ChartInfo::artist);
            text_field("Artist (unicode)", &ChartInfo::artist_unicode);
            text_field("Mapper", &ChartInfo::mapper);
            text_field("Difficulty", &ChartInfo::difficulty_name);
        }

        ImGui::SeparatorText("Gameplay");
        int_field("Keys", info.key_count, 1, 10, [](ChartInfo& edited, int value) { edited.key_count = (uint8_t)value; });

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Notes in lanes past the new key count are removed (undo brings them back)");
        }

        int_field("Preview (ms)", info.preview_time, -1, 3600000, [](ChartInfo& edited, int value) { edited.preview_time = value; });
        ImGui::SameLine();

        if (ImGui::SmallButton("Here##preview")) {
            int32_t here = (int32_t)std::lround(position);
            document->change([&](ChartData& edited) { edited.info.preview_time = here; });
        }

        int_field("Lead-in (ms)", info.audio_lead_in, 0, 10000, [](ChartInfo& edited, int value) { edited.audio_lead_in = value; });

        ImGui::SeparatorText("Files");
        ImGui::TextUnformatted(std::format("Audio: {}", info.audio_file.empty() ? "none" : info.audio_file).c_str());
        ImGui::SameLine();

        if (ImGui::SmallButton("Change##audio")) {
            dialog.open(FileDialog::Mode::OPEN, "Choose audio", document->directory(), AUDIO_EXTENSIONS);
            dialog_purpose = DialogPurpose::CHOOSE_AUDIO;
        }

        ImGui::TextUnformatted(std::format("Background: {}", info.image_file.empty() ? "none" : info.image_file).c_str());
        ImGui::SameLine();

        if (ImGui::SmallButton("Change##image")) {
            dialog.open(FileDialog::Mode::OPEN, "Choose background", document->directory(), IMAGE_EXTENSIONS);
            dialog_purpose = DialogPurpose::CHOOSE_IMAGE;
        }

        ImGui::SeparatorText("Stats");
        size_t holds = (size_t)std::count_if(data.hits.begin(), data.hits.end(), [](const Hit& hit) { return hit.hit_type == HitType::HOLD; });
        ImGui::TextUnformatted(std::format("{} notes, {} holds", data.hits.size() - holds, holds).c_str());

        if (!data.hits.empty()) {
            int32_t first = data.hits.front().time;
            int32_t last = first;

            for (const Hit& hit : data.hits) {
                last = std::max(last, hit.hit_type == HitType::HOLD ? hit.end_time : hit.time);
            }

            double seconds = std::max(1, last - first) / 1000.0;
            ImGui::TextUnformatted(std::format("Length {}, {:.2f} notes per second", clock_text(last - first), data.hits.size() / seconds).c_str());
        }

        if (difficulty.has_value()) {
            bool stale = difficulty_revision != document->revision() || difficulty_job.valid();

            if (!difficulty->error.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.4f, 1.0f), "Not playable yet: %s", difficulty->error.c_str());
            }
            else {
                ImGui::TextUnformatted(std::format("Difficulty {:.2f}{}", difficulty->rating, stale ? " (updating)" : "").c_str());
            }
        }
        else if (difficulty_job.valid()) {
            ImGui::TextDisabled("Rating...");
        }

        ImGui::End();
    }

    void EditorGame::draw_timing_panel() {
        if (!ImGui::Begin("Timing")) {
            ImGui::End();
            return;
        }

        if (document == nullptr) {
            ImGui::TextDisabled("No chart open");
            ImGui::End();
            return;
        }

        bool show_points = ImGui::CollapsingHeader("Timing points", ImGuiTreeNodeFlags_DefaultOpen);

        if (show_points && ImGui::Button("Add at playhead")) {
            const ChartTimingPoint* current = document->timing_point_at(position);
            ChartTimingPoint point = current != nullptr ? *current : ChartTimingPoint{0, 60000.0f / ChartDocument::DEFAULT_BPM, 4};
            point.time = (int32_t)std::lround(position);
            document->change([&](ChartData& edited) { edited.timing_points.push_back(point); });
        }

        if (show_points && tabs.size() > 1) {
            ImGui::SameLine();

            if (ImGui::Button("Copy to other difficulties")) {
                copy_timing_to_difficulties();
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Gives every other open difficulty of this song the same timing points (undo works in each)");
            }
        }

        const std::vector<ChartTimingPoint>& points = document->data().timing_points;
        const ChartTimingPoint* active_point = document->timing_point_at(position);
        std::optional<size_t> active = active_point != nullptr ? std::optional<size_t>((size_t)(active_point - points.data())) : std::nullopt;
        bool follow = active != shown_timing_point;
        shown_timing_point = active;
        std::function<void(ChartData&)> edit;
        auto move_to = [&](size_t index, int32_t time) {
            edit = [index, time](ChartData& edited) {
                std::vector<ChartTimingPoint>& list = edited.timing_points;
                list[index].time = time;
                ChartTimingPoint moved = list[index];
                std::erase_if(list, [&](const ChartTimingPoint& other) { return other.time == time; });
                list.push_back(moved);
            };
        };

        if (show_points && ImGui::BeginTable("##timing", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthStretch, 1.6f);
            ImGui::TableSetupColumn("BPM", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Meter", ImGuiTableColumnFlags_WidthStretch, 0.6f);
            ImGui::TableSetupColumn("##go", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##move", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("##delete", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (size_t index = 0; index < points.size(); index++) {
                const ChartTimingPoint& point = points[index];
                ImGui::PushID((int)index);
                ImGui::TableNextRow();

                if (active == index) {
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(255, 140, 60, 70));

                    if (follow) {
                        ImGui::SetScrollHereY(0.5f);
                    }
                }

                ImGui::TableNextColumn();
                int time = point.time;
                float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
                float button = ImGui::GetFrameHeight();
                int32_t nudge = ImGui::GetIO().KeyCtrl ? 10 : 1;
                ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - (button + spacing) * 2));
                ImGui::InputInt("##time", &time, 0, 0);

                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    move_to(index, time);
                }

                ImGui::SameLine(0, spacing);

                if (ImGui::Button("-", ImVec2(button, button))) {
                    move_to(index, point.time - nudge);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("1 ms earlier (Ctrl: 10 ms)");
                }

                ImGui::SameLine(0, spacing);

                if (ImGui::Button("+", ImVec2(button, button))) {
                    move_to(index, point.time + nudge);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("1 ms later (Ctrl: 10 ms)");
                }

                ImGui::TableNextColumn();
                float bpm = 60000.0f / point.beat_length;
                ImGui::SetNextItemWidth(-1);
                ImGui::InputFloat("##bpm", &bpm, 0.0f, 0.0f, "%.3f");

                if (ImGui::IsItemDeactivatedAfterEdit() && bpm > 1.0f && bpm < 10000.0f) {
                    edit = [index, bpm](ChartData& edited) {
                        edited.timing_points[index].beat_length = 60000.0f / bpm;
                    };
                }

                ImGui::TableNextColumn();
                int meter = point.meter;
                ImGui::SetNextItemWidth(-1);
                ImGui::InputInt("##meter", &meter, 0, 0);

                if (ImGui::IsItemDeactivatedAfterEdit() && meter >= 1 && meter <= 32) {
                    edit = [index, meter](ChartData& edited) {
                        edited.timing_points[index].meter = (uint8_t)meter;
                    };
                }

                ImGui::TableNextColumn();

                if (ImGui::SmallButton("Go")) {
                    seek(point.time);
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Move the playhead to this timing point");
                }

                ImGui::TableNextColumn();

                if (ImGui::SmallButton("Move here")) {
                    move_to(index, (int32_t)std::lround(position));
                }

                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Move this timing point to the playhead (%s)", clock_text(position).c_str());
                }

                ImGui::TableNextColumn();

                if (ImGui::SmallButton("X")) {
                    edit = [index](ChartData& edited) {
                        edited.timing_points.erase(edited.timing_points.begin() + (ptrdiff_t)index);
                    };
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        if (edit) {
            document->change(edit);
        }

        if (show_points && points.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.4f, 1.0f), "Add a timing point to snap notes to beats");
        }

        draw_tap_tempo();

        ImGui::End();
    }

    void EditorGame::tap_tempo() {
        double now = now_ms();
        bool from_song = playback.is_playing();

        if (!taps.empty() && (now - last_tap_at > TAP_RESET_MS || from_song != taps_from_song)) {
            taps.clear();
        }

        taps_from_song = from_song;
        last_tap_at = now;
        taps.push_back(from_song ? playback.position() : now);

        if (taps.size() > TAP_LIMIT) {
            taps.erase(taps.begin());
        }
    }

    void EditorGame::draw_tap_tempo() {
        ImGui::SeparatorText("Tap tempo");

        if (ImGui::Button("Tap", ImVec2(ui_px(110), ui_px(36)))) {
            tap_tempo();
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        std::optional<TapFit> fit = fit_taps(taps);
        bool stale = !taps.empty() && now_ms() - last_tap_at > TAP_RESET_MS;

        if (fit.has_value()) {
            double bpm = 60000.0 / fit->beat;
            ImGui::TextUnformatted(std::format("{:.2f} BPM", bpm).c_str());
            ImGui::TextDisabled("%s", std::format("{} taps, ±{:.1f} ms{}", taps.size(), fit->spread, stale ? ", next tap starts over" : "").c_str());
        }
        else {
            ImGui::TextDisabled("Press T or click Tap on each beat");
        }

        ImGui::EndGroup();

        if (!fit.has_value()) {
            return;
        }

        double bpm = 60000.0 / fit->beat;
        double chosen = round_tapped_bpm ? std::max(1.0, std::round(bpm)) : bpm;
        float beat_length = (float)(60000.0 / chosen);
        ImGui::Checkbox("Round BPM", &round_tapped_bpm);
        ImGui::SameLine();

        if (ImGui::Button("Apply BPM")) {
            const ChartTimingPoint* current = document->timing_point_at(position);

            if (current == nullptr) {
                document->change([&](ChartData& edited) { edited.timing_points.push_back({(int32_t)std::lround(taps_from_song ? fit->first : position), beat_length, 4}); });
            }
            else {
                size_t index = (size_t)(current - document->data().timing_points.data());
                document->change([&](ChartData& edited) { edited.timing_points[index].beat_length = beat_length; });
            }

            set_status(std::format("Set the BPM to {:.2f}", chosen));
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Sets the BPM of the timing point at the playhead");
        }

        ImGui::SameLine();

        if (ImGui::Button("Add timing point")) {
            int32_t time = (int32_t)std::lround(taps_from_song ? fit->first : position);
            const ChartTimingPoint* current = document->timing_point_at(time);
            uint8_t meter = current != nullptr ? current->meter : 4;
            document->change([&](ChartData& edited) {
                std::erase_if(edited.timing_points, [&](const ChartTimingPoint& point) { return point.time == time; });
                edited.timing_points.push_back({time, beat_length, meter});
            });
            set_status(std::format("Added a {:.2f} BPM timing point at {}", chosen, clock_text(time)));
        }

        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(taps_from_song ? "Starts at your first tap, which sets the offset" : "Starts at the playhead (tap while playing to set the offset from your taps)");
        }

        ImGui::SameLine();

        if (ImGui::Button("Reset")) {
            taps.clear();
        }
    }

    uint32_t EditorGame::autoplay_keys() const {
        uint32_t keys = 0;

        for (const Hit& hit : document->data().hits) {
            double end = hit.time + AUTOPLAY_TAP_MS;

            if (hit.hit_type == HitType::HOLD) {
                end = std::max(end, (double)hit.end_time);
            }

            if (position >= hit.time && position < end) {
                keys |= 1u << hit.key;
            }
        }

        return keys;
    }

    void EditorGame::draw_gameplay_panel() {
        if (!gameplay_open) {
            return;
        }

        ImGui::SetNextWindowSize(ImVec2(ui_px(GAMEPLAY_PANEL_SIZE.x), ui_px(GAMEPLAY_PANEL_SIZE.y)), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Gameplay preview", &gameplay_open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            ImGui::End();
            return;
        }

        if (document == nullptr || preview == nullptr || skin == nullptr) {
            ImGui::TextDisabled("Open a chart to see it as it plays in game");
            ImGui::End();
            return;
        }

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 area = ImGui::GetContentRegionAvail();

        if (area.x < 1.0f || area.y < 1.0f) {
            ImGui::End();
            return;
        }

        ImGui::Dummy(area);

        const Settings& settings = SettingsManager::settings;
        olc::vi2d screen = window->GetScreenSize();
        double scale = area.y / std::max(1, screen.y);
        uint8_t keys = std::max<uint8_t>(preview->info.key_count, 1);
        SkinLayoutScope layout_scope(skin.get(), keys);
        Ui::set_screen({std::max(1, (int32_t)std::lround(screen.x * scale)), std::max(1, (int32_t)std::lround(area.y))});

        std::array<olc::vd2d, 2> bounds = EntityField::gameplay_bounds(*skin, keys, {origin.x, origin.y}, {area.x, area.y});

        if (gameplay_field == nullptr) {
            gameplay_field = std::make_unique<EntityField>(preview.get(), skin.get(), bounds[0], bounds[1]);
        }

        EntityField& shown = *gameplay_field;
        shown.pos = bounds[0];
        shown.size = bounds[1];
        shown.apply_gameplay_settings(area.y);
        shown.draw_hit_score = false;
        shown.draw_combo = false;
        shown.draw_notes_past_judge = false;
        shown.scroll_speed = (uint32_t)std::max(1L, std::lround(settings.field_scroll_speed * area.y / std::max(1, screen.y)));
        shown.position = position;
        shown.set_keys(autoplay_keys());

        olc::Pixel background = skin->color(SkinColor::SCREEN_BACKGROUND);
        background.a = 255;
        window->SetDrawTarget(gameplay_layer);
        window->FillRectDecal({origin.x, origin.y}, {area.x, area.y}, background);
        shown.draw(window);
        window->SetDrawTarget(EDITOR_LAYER);
        Ui::set_screen(screen);

        std::vector<olc::DecalInstance>& decals = window->GetLayers()[gameplay_layer].vecDecalInstance;
        ImDrawList* list = ImGui::GetWindowDrawList();
        list->PushClipRect(origin, {origin.x + area.x, origin.y + area.y}, true);
        draw_decals(list, decals, screen, blank_decal.get());
        list->PopClipRect();
        decals.clear();
        ImGui::End();
    }

    void EditorGame::draw_help_panel() {
        if (!ImGui::Begin("Help")) {
            ImGui::End();
            return;
        }

        constexpr std::pair<const char*, const char*> MOUSE[] = {
            {"Left click", "place a note, drag up or down for a hold"},
            {"Drag a note", "move it (or the whole selection)"},
            {"Shift + drag", "select notes in a box (Ctrl too: add to it)"},
            {"Ctrl + click", "add or remove a note from the selection"},
            {"Right click", "delete a note"},
            {"Wheel", "move by one snap"},
            {"Alt + wheel", "move by one millisecond"},
            {"Shift + wheel", "change snap"},
            {"Ctrl + wheel", "change scroll speed"},
            {"Ctrl + Shift + wheel", "change lane width"},
        };

        if (ImGui::Button("Change key bindings...")) {
            settings_open = true;
        }

        if (ImGui::BeginTable("##help", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& [keys, action] : MOUSE) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(keys);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", action);
            }

            for (size_t index = 0; index < EDITOR_ACTION_COUNT; index++) {
                std::string keys = shortcut((EditorAction)index);

                if (keys.empty()) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(keys.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", action_info((EditorAction)index).label);
            }

            ImGui::EndTable();
        }

        ImGui::End();
    }

    void EditorGame::draw_dialogs() {
        if (document == nullptr && !dialog.is_open()) {
            ImGui::SetNextWindowPos(ImVec2(view.x + view.w / 2, view.y + view.h / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;

            if (ImGui::Begin("##welcome", nullptr, flags)) {
                ImGui::TextUnformatted("Nascent chart editor");
                ImGui::Spacing();

                if (ImGui::Button("New chart from audio...", ImVec2(ui_px(260), 0))) {
                    request(Pending::NEW_FROM_AUDIO);
                }

                if (ImGui::Button("Open chart...", ImVec2(ui_px(260), 0))) {
                    request(Pending::OPEN_CHART);
                }

                if (!recent_files.empty()) {
                    ImGui::Spacing();
                    ImGui::TextDisabled("Recent");

                    if (std::optional<std::filesystem::path> chosen = draw_recent_items()) {
                        pending_file = *chosen;
                        request(Pending::OPEN_CHART);
                    }
                }

                ImGui::Spacing();
                ImGui::TextDisabled("or drop a chart (.osu, .json, .sm, .ssc, .dwi) or audio file here");
            }

            ImGui::End();
        }

        if (std::optional<std::filesystem::path> chosen = dialog.draw()) {
            DialogPurpose purpose = std::exchange(dialog_purpose, DialogPurpose::NONE);
            std::string error;

            switch (purpose) {
                case DialogPurpose::OPEN_CHART:
                    open_chart(*chosen);
                    break;
                case DialogPurpose::NEW_FROM_AUDIO:
                    new_from_audio(*chosen);
                    break;
                case DialogPurpose::CHOOSE_AUDIO:
                    if (document != nullptr) {
                        document->set_audio(*chosen);
                        reload_audio();
                    }

                    break;
                case DialogPurpose::CHOOSE_IMAGE:
                    if (document != nullptr) {
                        document->set_image(*chosen);
                    }

                    break;
                case DialogPurpose::SAVE_AS:
                    if (document != nullptr && document->save_as(*chosen, error)) {
                        remember(*chosen);
                        set_status(std::format("Saved {}", path_utf8(chosen->filename())));

                        if (pending != Pending::NONE) {
                            request(pending);
                        }
                    }
                    else {
                        set_status(error, true);
                        pending = Pending::NONE;
                    }

                    break;
                case DialogPurpose::EXPORT_OSU:
                case DialogPurpose::EXPORT_STEPMANIA:
                case DialogPurpose::EXPORT_OSZ:
                case DialogPurpose::EXPORT_ZIP:
                    finish_export(purpose, *chosen);
                    break;
                case DialogPurpose::NONE:
                    break;
            }
        }
        else if (!dialog.is_open() && dialog_purpose != DialogPurpose::NONE) {
            if (dialog_purpose == DialogPurpose::SAVE_AS) {
                pending = Pending::NONE;
            }

            dialog_purpose = DialogPurpose::NONE;
        }

        if (choice_requested) {
            ImGui::OpenPopup("Choose a chart");
            choice_requested = false;
        }

        ImGui::SetNextWindowSize(ImVec2(ui_px(560), 0), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Choose a chart", nullptr)) {
            ImGui::TextUnformatted(std::format("{} has {} charts:", path_utf8(choice_source.filename()), chart_choices.size()).c_str());
            ImGui::Spacing();
            std::optional<size_t> picked;

            if (ImGui::BeginChild("##choices", ImVec2(0, std::min(ui_px(360.0f), ImGui::GetFrameHeightWithSpacing() * (float)chart_choices.size() + 8.0f)), ImGuiChildFlags_Borders)) {
                for (size_t index = 0; index < chart_choices.size(); index++) {
                    const ChartDocument::ChartChoice& choice = chart_choices[index];
                    ImGui::PushID((int)index);
                    ImGui::BeginDisabled(!choice.error.empty());

                    if (ImGui::Selectable(choice.label.c_str())) {
                        picked = index;
                    }

                    ImGui::EndDisabled();

                    if (!choice.error.empty()) {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s)", choice.error.c_str());
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndChild();

            if (picked.has_value()) {
                ImGui::CloseCurrentPopup();
                open_choice(*picked);
            }

            if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
                chart_choices.clear();
            }

            ImGui::EndPopup();
        }

        if (osu_prompt_requested) {
            ImGui::OpenPopup("osu! difficulty");
            osu_prompt_requested = false;
        }

        if (ImGui::BeginPopupModal("osu! difficulty", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("osu! stores its own judgement and HP drain in the chart.");
            ImGui::TextDisabled("These only go into the exported file, the chart itself doesn't change.");
            ImGui::Spacing();
            ImGui::SetNextItemWidth(ui_px(260));
            ImGui::SliderFloat("Overall difficulty (OD)", &osu_difficulty.overall_difficulty, 0.0f, 10.0f, "%.1f");
            ImGui::SetNextItemWidth(ui_px(260));
            ImGui::SliderFloat("HP drain", &osu_difficulty.hp_drain, 0.0f, 10.0f, "%.1f");
            osu_difficulty.overall_difficulty = std::round(osu_difficulty.overall_difficulty * 10.0f) / 10.0f;
            osu_difficulty.hp_drain = std::round(osu_difficulty.hp_drain * 10.0f) / 10.0f;
            ImGui::Spacing();

            if (ImGui::Button("Continue", ImVec2(ui_px(110), 0))) {
                ImGui::CloseCurrentPopup();

                if (document != nullptr) {
                    bool archive = osu_prompt_purpose == DialogPurpose::EXPORT_OSZ;
                    open_export(osu_prompt_purpose, archive ? "Export .osz" : "Export .osu", archive ? ".osz" : ".osu");
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        draw_transform_popup();
        draw_quantize_popup();
        draw_inverse_popup();
        draw_new_difficulty_popup();
        draw_goto_popup();
        draw_pattern_window();
        draw_library_window();
        draw_checker_window();

        if (audio_prompt_requested) {
            ImGui::OpenPopup("Dropped audio");
            audio_prompt_requested = false;
        }

        if (ImGui::BeginPopupModal("Dropped audio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(std::format("What should {} be used for?", path_utf8(dropped_audio.filename())).c_str());
            ImGui::Spacing();

            if (ImGui::Button("Change this chart's music", ImVec2(ui_px(220), 0))) {
                ImGui::CloseCurrentPopup();

                if (document != nullptr) {
                    document->set_audio(dropped_audio);
                    reload_audio();
                    set_status(std::format("Audio changed to {}", path_utf8(dropped_audio.filename())));
                }
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Keeps the notes and timing, only the song changes (undo with Ctrl + Z)");
            }

            ImGui::SameLine();

            if (ImGui::Button("Start a new chart", ImVec2(ui_px(160), 0))) {
                ImGui::CloseCurrentPopup();
                pending_file = dropped_audio;
                request(Pending::NEW_FROM_AUDIO);
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (confirm_requested) {
            ImGui::OpenPopup("Unsaved changes");
            confirm_requested = false;
        }

        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            bool closing = pending == Pending::CLOSE_TAB;
            size_t count = closing ? 1 : dirty_count();
            ImGui::TextUnformatted(count > 1 ? std::format("{} difficulties have changes that aren't saved.", count).c_str() : closing ? "This difficulty has changes that aren't saved."
                                                                                                                                       : "This chart has changes that aren't saved.");
            ImGui::Spacing();

            if (ImGui::Button(count > 1 ? "Save all" : "Save", ImVec2(ui_px(110), 0))) {
                ImGui::CloseCurrentPopup();

                if (closing) {
                    if (document != nullptr && document->path().empty()) {
                        save_as();
                    }
                    else {
                        save();

                        if (document == nullptr || !document->dirty()) {
                            perform(pending);
                        }
                    }
                }
                else {
                    save_all();
                    std::optional<size_t> unsaved;

                    for (size_t index = 0; index < tabs.size() && !unsaved; index++) {
                        if (const ChartDocument* chart = tab_document(index); chart != nullptr && chart->dirty()) {
                            unsaved = index;
                        }
                    }

                    if (unsaved) {
                        switch_tab(*unsaved);
                        save_as();
                    }
                    else {
                        perform(pending);
                    }
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Discard", ImVec2(ui_px(110), 0))) {
                ImGui::CloseCurrentPopup();
                perform(pending);
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
                pending = Pending::NONE;
                pending_file.clear();
            }

            ImGui::EndPopup();
        }
    }

    void EditorGame::open_export(DialogPurpose purpose, const char* title, const char* extension) {
        std::filesystem::path name = document->suggested_file_name().replace_extension(extension);
        dialog.open(FileDialog::Mode::SAVE, title, document->directory(), {extension}, path_utf8(name));
        dialog_purpose = purpose;
    }

    void EditorGame::finish_export(DialogPurpose purpose, const std::filesystem::path& path) {
        if (document == nullptr) {
            return;
        }

        std::string error;
        int32_t meter = difficulty.has_value() && difficulty->error.empty() ? (int32_t)std::lround(difficulty->rating) : 1;
        bool exported = false;

        switch (purpose) {
            case DialogPurpose::EXPORT_OSU:
                exported = export_osu(document->data(), osu_difficulty, path, error);
                break;
            case DialogPurpose::EXPORT_STEPMANIA:
                exported = export_stepmania(document->data(), path, meter, error);
                break;
            case DialogPurpose::EXPORT_OSZ:
                exported = export_archive(document->data(), document->directory(), path, ArchiveFormat::OSZ, osu_difficulty, error);
                break;
            case DialogPurpose::EXPORT_ZIP:
                exported = export_archive(document->data(), document->directory(), path, ArchiveFormat::NASCENT, osu_difficulty, error);
                break;
            default:
                return;
        }

        if (!exported) {
            set_status(error, true);
            return;
        }

        bool archive = purpose == DialogPurpose::EXPORT_OSZ || purpose == DialogPurpose::EXPORT_ZIP;
        bool elsewhere = !archive && path.parent_path() != document->directory();
        set_status(elsewhere ? std::format("Exported {}, copy the audio next to it so the other game can find it", path_utf8(path.filename())) : std::format("Exported {}", path_utf8(path.filename())));
    }

    void EditorGame::handle_dropped_files() {
        for (const std::string& dropped : input().dropped_files()) {
            std::filesystem::path path = utf8_path(dropped);

            if (has_extension(path, CHART_EXTENSIONS)) {
                pending_file = path;
                request(Pending::OPEN_CHART);
                return;
            }

            if (has_extension(path, AUDIO_EXTENSIONS)) {
                if (document != nullptr) {
                    dropped_audio = path;
                    audio_prompt_requested = true;
                }
                else {
                    pending_file = path;
                    request(Pending::NEW_FROM_AUDIO);
                }

                return;
            }
        }
    }

    void EditorGame::handle_shortcuts() {
        ImGuiIO& io = ImGui::GetIO();

        if (io.WantTextInput || dialog.is_open() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) || capturing) {
            return;
        }

        Input& keys = input();
        bool alt = alt_held();
        auto pressed = [&](EditorAction action) {
            return bindings.pressed(action, keys, alt);
        };

        if (pressed(EditorAction::NEW_FROM_AUDIO)) {
            request(Pending::NEW_FROM_AUDIO);
        }

        if (pressed(EditorAction::OPEN_CHART)) {
            request(Pending::OPEN_CHART);
        }

        if (pressed(EditorAction::QUIT)) {
            request(Pending::QUIT);
        }

        if (pressed(EditorAction::SETTINGS)) {
            settings_open = true;
            ImGui::SetWindowFocus("Settings");
        }

        if (document == nullptr) {
            return;
        }

        if (pressed(EditorAction::SAVE)) {
            save();
        }

        if (pressed(EditorAction::SAVE_AS)) {
            save_as();
        }

        if (pressed(EditorAction::TEST_PLAY)) {
            start_test_play();
        }

        if (pressed(EditorAction::UNDO)) {
            document->undo();
        }

        if (pressed(EditorAction::REDO)) {
            document->redo();
        }

        if (pressed(EditorAction::TRANSFORM)) {
            if (selection.empty()) {
                set_status("Select some notes first (Shift + drag, or Ctrl + A)", true);
            }
            else {
                transform_requested = true;
            }
        }

        if (pressed(EditorAction::INVERSE)) {
            if (selection.empty()) {
                set_status("Select some notes first (Shift + drag, or Ctrl + A)", true);
            }
            else {
                inverse_requested = true;
            }
        }

        if (pressed(EditorAction::SELECT_ALL)) {
            select_all();
        }

        if (pressed(EditorAction::COPY)) {
            copy_selection();
        }

        if (pressed(EditorAction::CUT)) {
            cut_selection();
        }

        if (pressed(EditorAction::PASTE)) {
            paste_clipboard();
        }

        if (pressed(EditorAction::LANE_WIDER)) {
            change_lane_width(1);
        }

        if (pressed(EditorAction::LANE_NARROWER)) {
            change_lane_width(-1);
        }

        if (pressed(EditorAction::NUDGE_LATER)) {
            nudge_selection(1, 0);
        }

        if (pressed(EditorAction::NUDGE_EARLIER)) {
            nudge_selection(-1, 0);
        }

        if (pressed(EditorAction::NUDGE_LEFT)) {
            nudge_selection(0, -1);
        }

        if (pressed(EditorAction::NUDGE_RIGHT)) {
            nudge_selection(0, 1);
        }

        if (pressed(EditorAction::SCROLL_FASTER)) {
            change_scroll_speed(1);
        }

        if (pressed(EditorAction::SCROLL_SLOWER)) {
            change_scroll_speed(-1);
        }

        if (pressed(EditorAction::PLAY_PAUSE) && playback.loaded()) {
            toggle_playback();
        }

        if (pressed(EditorAction::MS_FORWARD)) {
            seek(position + 1);
        }

        if (pressed(EditorAction::MS_BACK)) {
            seek(position - 1);
        }

        if (gesture.kind == Gesture::Kind::NONE && pressed(EditorAction::FINER_SNAP)) {
            change_snap(1);
        }

        if (gesture.kind == Gesture::Kind::NONE && pressed(EditorAction::COARSER_SNAP)) {
            change_snap(-1);
        }

        if (pressed(EditorAction::STEP_BACK)) {
            step(-1);
        }

        if (pressed(EditorAction::STEP_FORWARD)) {
            step(1);
        }

        if (pressed(EditorAction::BEAT_BACK)) {
            step(-(int32_t)divisor);
        }

        if (pressed(EditorAction::BEAT_FORWARD)) {
            step((int32_t)divisor);
        }

        if (pressed(EditorAction::GO_TO_START)) {
            seek(0);
        }

        if (pressed(EditorAction::GO_TO_END) && !document->data().hits.empty()) {
            seek(document->data().hits.back().time);
        }

        if (pressed(EditorAction::TAP_TEMPO)) {
            tap_tempo();
        }

        if (pressed(EditorAction::MIRROR)) {
            mirror_selection();
        }

        if (pressed(EditorAction::TOGGLE_SNAP)) {
            snap_enabled = !snap_enabled;
            set_status(snap_enabled ? "Snapping notes to the grid" : "Not snapping, notes go to the exact millisecond");
        }

        if (pressed(EditorAction::CLEAR_SELECTION)) {
            selection.clear();
        }

        if (pressed(EditorAction::DELETE_SELECTION)) {
            delete_selection();
        }

        if (pressed(EditorAction::COPY_TIMESTAMP)) {
            copy_timestamp();
        }

        if (pressed(EditorAction::GO_TO_TIMESTAMP)) {
            go_to_clipboard_timestamp();
        }

        if (pressed(EditorAction::NEXT_DIFFICULTY)) {
            step_tab(1);
        }

        if (pressed(EditorAction::PREVIOUS_DIFFICULTY)) {
            step_tab(-1);
        }

        if (document == nullptr) {
            return;
        }

        uint8_t key_count = document->data().info.key_count;

        for (uint8_t lane = 0; lane < key_count && lane < 10; lane++) {
            if (!pressed((EditorAction)((size_t)EditorAction::LANE_1 + lane))) {
                continue;
            }

            int32_t time = (int32_t)std::lround(place_time(position));
            document->change([&](ChartData& data) {
                auto existing = std::find_if(data.hits.begin(), data.hits.end(), [&](const Hit& hit) { return hit.key == lane && hit.time == time; });

                if (existing != data.hits.end()) {
                    data.hits.erase(existing);
                }
                else {
                    data.hits.push_back({lane, HitType::HIT, time, time});
                }
            });
        }
    }

    void EditorGame::layout_field() {
        if (field == nullptr || preview == nullptr) {
            return;
        }

        uint8_t keys = std::max<uint8_t>(preview->info.key_count, 1);
        SkinLayoutScope layout_scope(skin.get(), keys);
        olc::vi2d screen = window->GetScreenSize();
        double lane = std::min(screen.y * lane_width_percent() / 100.0, (double)view.w / keys);
        double width = std::max(1.0, lane * keys);

        field->pos = {view.x + (view.w - width) / 2.0, (double)view.y};
        field->size = {width, std::max(1.0, (double)view.h)};
        field->judge_height = view.h * judge_offset;
        field->set_lane_gap(lane_gap_setting());
        field->position = position;
        field->scroll_speed = scroll_speed;
    }

    void EditorGame::handle_field_input() {
        hover_column.reset();
        show_ghost = false;

        if (field == nullptr || document == nullptr) {
            gesture = {};
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        Input& keys = input();
        olc::vi2d mouse = keys.mouse_position();
        olc::vf2d point = {(float)mouse.x, (float)mouse.y};
        bool over_view = point.x >= view.x && point.x < view.x + view.w && point.y >= view.y && point.y < view.y + view.h;
        bool free = !io.WantCaptureMouse && !dialog.is_open();
        uint8_t key_count = preview->info.key_count;
        double per_ms = std::max(1e-6, field->rate(window));
        double raw_time = field->time_at(point.y, window);
        double snapped = place_time(raw_time);
        std::optional<uint8_t> column = field->column_at(point.x);
        prune_selection();

        if (free && over_view) {
            if (int32_t wheel = keys.mouse_wheel(); wheel != 0) {
                int32_t notches = wheel > 0 ? std::max(1, wheel / 120) : std::min(-1, wheel / 120);

                if (alt_held()) {
                    seek(position + notches);
                }
                else if (gesture.kind != Gesture::Kind::NONE) {
                    step(notches);
                }
                else if (keys.key_held(olc::Key::CTRL) && keys.key_held(olc::Key::SHIFT)) {
                    change_lane_width(notches);
                }
                else if (keys.key_held(olc::Key::CTRL)) {
                    change_scroll_speed(notches);
                }
                else if (keys.key_held(olc::Key::SHIFT)) {
                    change_snap(notches);
                }
                else {
                    step(notches);
                }
            }
        }

        if (gesture.kind != Gesture::Kind::NONE && keys.mouse(0).bHeld && !playback.is_playing()) {
            float above = view.y + AUTO_SCROLL_EDGE_PX - point.y;
            float below = point.y - (view.y + view.h - AUTO_SCROLL_EDGE_PX);
            double depth = above > 0 ? above / AUTO_SCROLL_EDGE_PX : below > 0 ? -below / AUTO_SCROLL_EDGE_PX
                                                                               : 0.0;

            if (depth != 0.0) {
                double pixels = std::clamp(depth, -AUTO_SCROLL_MAX_DEPTH, AUTO_SCROLL_MAX_DEPTH) * AUTO_SCROLL_PX_PER_SECOND * io.DeltaTime;
                seek(position + pixels / per_ms);
            }
        }

        if (free && field->contains(point) && column.has_value()) {
            hover_column = column;
            hover_time = snapped;
            show_ghost = gesture.kind == Gesture::Kind::NONE;
        }

        uint8_t nearest_column = column.value_or(point.x < field->pos.x ? 0 : (uint8_t)(key_count - 1));

        if (gesture.kind == Gesture::Kind::NONE && free && over_view && keys.mouse(0).bPressed && keys.key_held(olc::Key::SHIFT)) {
            gesture = {};
            gesture.kind = Gesture::Kind::SELECT;
            gesture.key = gesture.end_key = nearest_column;
            gesture.start = gesture.end = raw_time;
            gesture.additive = keys.key_held(olc::Key::CTRL);
        }

        if (gesture.kind == Gesture::Kind::NONE && free && hover_column.has_value()) {
            double tolerance = field->note_height() * 0.5 / per_ms;
            bool ctrl = keys.key_held(olc::Key::CTRL);

            if (keys.mouse(0).bPressed) {
                if (std::optional<size_t> index = document->note_at(*hover_column, raw_time, tolerance)) {
                    Hit hit = document->data().hits[*index];

                    if (ctrl) {
                        if (is_selected(hit)) {
                            std::erase_if(selection, [&](const Hit& other) { return same_hit(other, hit); });
                        }
                        else {
                            selection.push_back(hit);
                        }
                    }
                    else {
                        if (!is_selected(hit)) {
                            selection = {hit};
                        }

                        gesture = {};
                        gesture.kind = Gesture::Kind::MOVE;
                        gesture.grabbed = hit;
                        gesture.grab_offset = raw_time - hit.time;
                        gesture.originals = selection;
                        ChartData base;
                        base.hits = document->data().hits;
                        replace_hits(base, selection, {});
                        gesture.base = std::move(base.hits);
                    }
                }
                else if (!ctrl) {
                    selection.clear();
                    gesture = {};
                    gesture.kind = Gesture::Kind::PLACE;
                    gesture.key = *hover_column;
                    gesture.start = gesture.end = hover_time;
                }
            }
            else if (keys.mouse(1).bPressed) {
                if (std::optional<size_t> index = document->note_at(*hover_column, raw_time, tolerance)) {
                    Hit target = document->data().hits[*index];
                    document->change([&](ChartData& data) {
                        std::erase_if(data.hits, [&](const Hit& hit) { return same_hit(hit, target); });
                    });

                    std::erase_if(selection, [&](const Hit& other) { return same_hit(other, target); });
                }
            }
        }

        if (gesture.kind == Gesture::Kind::PLACE) {
            gesture.end = place_time(raw_time);

            if (!keys.mouse(0).bHeld) {
                double start = std::min(gesture.start, gesture.end);
                double end = std::max(gesture.start, gesture.end);
                bool hold = end - start >= (snap_enabled ? document->snap_length(start, divisor) * 0.5 : field->note_height() / per_ms);
                Hit hit = {gesture.key, hold ? HitType::HOLD : HitType::HIT, (int32_t)std::lround(start), (int32_t)std::lround(hold ? end : start)};
                document->change([&](ChartData& data) {
                    std::erase_if(data.hits, [&](const Hit& existing) { return existing.key == hit.key && existing.time == hit.time; });
                    data.hits.push_back(hit);
                });
                selection = {hit};
                gesture = {};
            }
        }
        else if (gesture.kind == Gesture::Kind::MOVE) {
            if (!keys.mouse(0).bHeld) {
                gesture = {};
            }
            else {
                int32_t target_time = (int32_t)std::lround(place_time(raw_time - gesture.grab_offset));
                int32_t current_key = (int32_t)gesture.grabbed.key + gesture.key_offset;
                int32_t target_key = column.has_value() ? (int32_t)*column : current_key;
                auto [time_offset, key_offset] = clamp_offsets(gesture.originals, target_time - gesture.grabbed.time, target_key - (int32_t)gesture.grabbed.key);

                if (time_offset != gesture.time_offset || key_offset != gesture.key_offset) {
                    if (!gesture.moved) {
                        document->snapshot();
                        gesture.moved = true;
                    }

                    std::vector<Hit> moved = shifted_hits(gesture.originals, time_offset, key_offset);
                    document->change_without_snapshot([&](ChartData& data) {
                        data.hits = gesture.base;
                        replace_hits(data, {}, moved);
                    });
                    selection = std::move(moved);
                    selection_revision = document->revision();
                    gesture.time_offset = time_offset;
                    gesture.key_offset = key_offset;
                }
            }
        }
        else if (gesture.kind == Gesture::Kind::SELECT) {
            gesture.end = raw_time;
            gesture.end_key = nearest_column;

            if (!keys.mouse(0).bHeld) {
                double tolerance = field->note_height() * 0.5 / per_ms;
                std::vector<Hit> found = document->notes_in(std::min(gesture.key, gesture.end_key), std::max(gesture.key, gesture.end_key), std::min(gesture.start, gesture.end) - tolerance, std::max(gesture.start, gesture.end) + tolerance);

                if (!gesture.additive) {
                    selection.clear();
                }

                for (const Hit& hit : found) {
                    if (!is_selected(hit)) {
                        selection.push_back(hit);
                    }
                }

                gesture = {};
            }
        }
    }

    bool EditorGame::is_selected(const Hit& hit) const {
        return std::any_of(selection.begin(), selection.end(), [&](const Hit& other) { return same_hit(other, hit); });
    }

    void EditorGame::prune_selection() {
        if (document == nullptr || selection_revision == document->revision()) {
            return;
        }

        std::erase_if(selection, [&](const Hit& hit) { return !document->contains(hit); });
        selection_revision = document->revision();
    }

    std::pair<int32_t, int32_t> EditorGame::clamp_offsets(const std::vector<Hit>& hits, int32_t time_offset, int32_t key_offset) const {
        if (hits.empty()) {
            return {0, 0};
        }

        int32_t earliest = INT32_MAX;
        int32_t lowest = INT32_MAX;
        int32_t highest = 0;

        for (const Hit& hit : hits) {
            earliest = std::min(earliest, hit.time);
            lowest = std::min<int32_t>(lowest, hit.key);
            highest = std::max<int32_t>(highest, hit.key);
        }

        int32_t key_count = document->data().info.key_count;
        return {std::max(time_offset, -earliest), std::clamp(key_offset, -lowest, key_count - 1 - highest)};
    }

    void EditorGame::move_selection(int32_t time_offset, int32_t key_offset) {
        prune_selection();
        auto [clamped_time, clamped_key] = clamp_offsets(selection, time_offset, key_offset);

        if (clamped_time == 0 && clamped_key == 0) {
            return;
        }

        std::vector<Hit> moved = shifted_hits(selection, clamped_time, clamped_key);
        document->change([&](ChartData& data) { replace_hits(data, selection, moved); });
        selection = std::move(moved);
        selection_revision = document->revision();
    }

    void EditorGame::nudge_selection(int32_t steps, int32_t lanes) {
        prune_selection();

        if (selection.empty()) {
            return;
        }

        int32_t time_offset = 0;

        if (steps != 0) {
            int32_t earliest = std::min_element(selection.begin(), selection.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; })->time;
            time_offset = snap_enabled ? (int32_t)std::lround(document->step(earliest, divisor, steps)) - earliest : steps;
        }

        move_selection(time_offset, lanes);
    }

    void EditorGame::mirror_selection() {
        prune_selection();

        if (document == nullptr || selection.empty()) {
            return;
        }

        std::vector<Hit> mirrored = mirrored_hits(selection, document->data().info.key_count);
        document->change([&](ChartData& data) { replace_hits(data, selection, mirrored); });
        selection = std::move(mirrored);
        selection_revision = document->revision();
        set_status(std::format("Mirrored {} notes", selection.size()));
    }

    void EditorGame::stretch_selection(double factor, bool from_playhead) {
        prune_selection();

        if (document == nullptr || selection.empty() || !(factor > 0)) {
            return;
        }

        int32_t earliest = std::min_element(selection.begin(), selection.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; })->time;
        double anchor = from_playhead ? position : (double)earliest;
        std::vector<Hit> result = document->stretched(selection, factor, anchor);
        auto [time_offset, key_offset] = clamp_offsets(result, 0, 0);
        result = shifted_hits(result, time_offset, key_offset);
        document->change([&](ChartData& data) { replace_hits(data, selection, result); });
        selection = std::move(result);
        selection_revision = document->revision();
        set_status(std::format("{} {} notes {:g}x", factor >= 1.0 ? "Stretched" : "Shrunk", selection.size(), factor >= 1.0 ? factor : 1.0 / factor));
    }

    bool EditorGame::snap_picker(const char* label, uint32_t& value) {
        bool changed = false;

        if (ImGui::BeginCombo(label, std::format("1/{}", value).c_str(), ImGuiComboFlags_HeightLarge)) {
            auto options = [&](const auto& divisors) {
                for (uint32_t option : divisors) {
                    if (ImGui::Selectable(std::format("1/{}", option).c_str(), option == value)) {
                        value = option;
                        changed = true;
                    }
                }
            };

            options(DIVISORS);
            ImGui::SeparatorText("Complex tuplets");
            options(COMPLEX_TUPLET_DIVISORS);
            ImGui::EndCombo();
        }

        return changed;
    }

    std::vector<Hit> EditorGame::pattern_hits() const {
        std::vector<Hit> hits;

        if (document == nullptr || document->timing_point_at(position) == nullptr) {
            return hits;
        }

        const PatternVariant& variant = pattern_styles()[pattern_style].variants[pattern_variant];
        double time = std::max(0.0, document->snap(position, pattern_divisor));
        size_t rows = std::min<size_t>((size_t)pattern_beats * pattern_divisor, MAX_PATTERN_ROWS);

        for (const PatternRow& row : generate_pattern(variant.family, document->data().info.key_count, rows, pattern_seed, pattern_settings)) {
            int32_t at = (int32_t)std::lround(time);

            for (uint8_t key : row) {
                hits.push_back({key, HitType::HIT, at, at});
            }

            double next = document->step(time, pattern_divisor, 1);

            if (next <= time) {
                break;
            }

            time = next;
        }

        return hits;
    }

    void EditorGame::place_pattern() {
        std::vector<Hit> generated = pattern_hits();

        if (generated.empty()) {
            return;
        }

        int32_t first = generated.front().time;
        int32_t last = generated.back().time;
        std::vector<Hit> replaced;

        if (pattern_replace) {
            for (const Hit& hit : document->data().hits) {
                if (hit.time >= first && hit.time <= last) {
                    replaced.push_back(hit);
                }
            }
        }

        document->change([&](ChartData& data) { replace_hits(data, replaced, generated); });
        selection = generated;
        selection_revision = document->revision();
        pattern_seed = std::random_device{}();
        set_status(std::format("Placed {} notes of {} at {}", generated.size(), pattern_styles()[pattern_style].variants[pattern_variant].name, clock_text(first)));
    }

    void EditorGame::load_library() {
        std::filesystem::path path = settings_directory / LIBRARY_FILE;
        std::string error;
        library = load_pattern_library(path, error);

        if (!error.empty()) {
            std::filesystem::path backup = settings_directory / "editor_patterns.damaged.json";
            std::error_code ec;
            std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing, ec);
            set_status(std::format("Couldn't read the pattern library ({}), kept a copy as {}", error, path_utf8(backup.filename())), true);
        }
    }

    void EditorGame::save_library() {
        std::string error;

        if (!save_pattern_library(settings_directory / LIBRARY_FILE, library, error)) {
            set_status(std::format("Couldn't save the pattern library: {}", error), true);
        }
    }

    void EditorGame::open_library(bool saving) {
        library_open = true;
        library_focus_name = saving;
        ImGui::SetWindowFocus("Pattern library");
    }

    void EditorGame::add_to_library() {
        prune_selection();

        if (document == nullptr || selection.empty()) {
            set_status("Select some notes first (Shift + drag, or Ctrl + A)", true);
            return;
        }

        std::string name = library_name;
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        if (name.empty()) {
            name = std::format("Pattern {}", library.size() + 1);
        }

        library.push_back(capture_pattern(*document, selection, name));
        library_selected = library.size() - 1;
        library_lane_offset = 0;
        library_name.clear();
        save_library();
        set_status(std::format("Saved {} notes to the pattern library as \"{}\"", library.back().notes.size(), name));
    }

    std::vector<Hit> EditorGame::library_hits() const {
        if (document == nullptr || !library_selected.has_value() || *library_selected >= library.size()) {
            return {};
        }

        return library_pattern_hits(*document, library[*library_selected], std::max(0.0, place_time(position)), library_lane_offset);
    }

    void EditorGame::place_from_library() {
        std::vector<Hit> placed = library_hits();

        if (placed.empty()) {
            return;
        }

        std::vector<Hit> replaced;

        if (library_replace) {
            auto [low, high] = std::minmax_element(placed.begin(), placed.end(), [](const Hit& a, const Hit& b) { return a.key < b.key; });
            int32_t last = 0;

            for (const Hit& hit : placed) {
                last = std::max(last, hit.hit_type == HitType::HOLD ? hit.end_time : hit.time);
            }

            replaced = document->notes_in(low->key, high->key, placed.front().time, last);
        }

        document->change([&](ChartData& data) { replace_hits(data, replaced, placed); });
        selection = placed;
        selection_revision = document->revision();
        set_status(std::format("Placed {} ({} notes) at {}", library[*library_selected].name, placed.size(), clock_text(placed.front().time)));
    }

    void EditorGame::draw_library_window() {
        if (!library_open) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.35f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin("Pattern library", &library_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::End();
            return;
        }

        prune_selection();
        bool open = document != nullptr;
        bool can_save = open && !selection.empty();
        uint8_t keys = open ? document->data().info.key_count : 0;

        ImGui::SeparatorText("Save selection");

        if (library_focus_name) {
            ImGui::SetKeyboardFocusHere();
            library_focus_name = false;
        }

        ImGui::SetNextItemWidth(ui_px(260));
        bool entered = ImGui::InputTextWithHint("##library_name", std::format("Pattern {}", library.size() + 1).c_str(), &library_name, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(!can_save);

        if (ImGui::Button("Save", ImVec2(ui_px(110), 0)) || (entered && can_save)) {
            add_to_library();
        }

        ImGui::EndDisabled();
        ImGui::TextDisabled("%s", can_save ? std::format("Saves the {} selected notes", selection.size()).c_str() : "Select some notes to save them here");

        ImGui::SeparatorText("Saved patterns");
        ImGui::SetNextItemWidth(ui_px(260));
        ImGui::InputTextWithHint("##library_filter", "Search", &library_filter);

        if (open) {
            ImGui::SameLine();
            ImGui::Checkbox("All key counts", &library_all_keys);
        }

        std::string filter = lowercase(library_filter);
        size_t shown = 0;

        if (ImGui::BeginChild("##library_list", ImVec2(ui_px(460), ui_px(220)), ImGuiChildFlags_Borders)) {
            for (size_t index = 0; index < library.size(); index++) {
                const LibraryPattern& pattern = library[index];

                if ((open && !library_all_keys && pattern.key_count != keys) || lowercase(pattern.name).find(filter) == std::string::npos) {
                    continue;
                }

                shown++;

                if (ImGui::Selectable(std::format("{}##library{}", pattern.name.empty() ? "Untitled" : pattern.name, index).c_str(), library_selected == index, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (library_selected != index) {
                        library_lane_offset = 0;
                    }

                    library_selected = index;

                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && open) {
                        place_from_library();
                    }
                }

                ImGui::SameLine(270);
                ImGui::TextDisabled("%s", std::format("{}K, {} notes, {:.4g} beats", pattern.key_count, pattern.notes.size(), pattern_length_beats(pattern)).c_str());
            }

            if (shown == 0) {
                ImGui::TextDisabled("%s", library.empty() ? "Nothing saved yet" : "No saved patterns match");
            }
        }

        ImGui::EndChild();

        if (library_selected.has_value() && *library_selected < library.size()) {
            LibraryPattern& pattern = library[*library_selected];
            ImGui::SetNextItemWidth(ui_px(260));
            ImGui::InputText("Name##library_rename", &pattern.name);

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                save_library();
            }

            if (open) {
                auto [low, high] = pattern_lanes(pattern);
                int lowest = std::min(-(int)low, (int)keys - 1 - (int)high);
                int highest = std::max(-(int)low, (int)keys - 1 - (int)high);
                library_lane_offset = std::clamp(library_lane_offset, lowest, highest);

                if (lowest != highest) {
                    ImGui::SetNextItemWidth(ui_px(260));
                    ImGui::SliderInt("Lane offset", &library_lane_offset, lowest, highest);
                    library_lane_offset = std::clamp(library_lane_offset, lowest, highest);
                }

                ImGui::Checkbox("Replace notes already there", &library_replace);
                std::vector<Hit> hits = library_hits();
                pattern_preview.insert(pattern_preview.end(), hits.begin(), hits.end());

                if (!hits.empty()) {
                    ImGui::TextUnformatted(std::format("{} notes from {} at the playhead", hits.size(), clock_text(hits.front().time)).c_str());
                }

                if (hits.size() < pattern.notes.size()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", std::format("{} notes don't fit in this {}K chart and are left out", pattern.notes.size() - hits.size(), keys).c_str());
                }

                ImGui::BeginDisabled(hits.empty());

                if (ImGui::Button("Place at playhead", ImVec2(ui_px(160), 0))) {
                    place_from_library();
                }

                ImGui::EndDisabled();
                ImGui::SameLine();
            }

            if (ImGui::Button("Delete", ImVec2(ui_px(110), 0))) {
                library_delete_requested = true;
            }
        }

        if (library_delete_requested) {
            ImGui::OpenPopup("Delete pattern");
            library_delete_requested = false;
        }

        if (ImGui::BeginPopupModal("Delete pattern", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            bool valid = library_selected.has_value() && *library_selected < library.size();

            if (valid) {
                ImGui::TextUnformatted(std::format("Delete \"{}\" from the pattern library? This can't be undone.", library[*library_selected].name).c_str());
            }

            if (ImGui::Button("Delete", ImVec2(ui_px(110), 0)) && valid) {
                std::string name = library[*library_selected].name;
                library.erase(library.begin() + (std::ptrdiff_t)*library_selected);
                library_selected.reset();
                save_library();
                set_status(std::format("Deleted \"{}\" from the pattern library", name));
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !valid) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void EditorGame::update_checker() {
        if (!checker_open || document == nullptr) {
            return;
        }

        if (hands_job.valid() && hands_job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            HandRatings rated = hands_job.get();

            if (hands_job_key.first == document_serial) {
                hands = rated;
                hands_key = hands_job_key;
            }
        }

        std::pair<uint64_t, uint64_t> key = {document_serial, document->revision()};

        if (!hands_job.valid() && hands_key != key && hands_job_key != key && now_ms() - changed_at >= DIFFICULTY_DELAY_MS) {
            hands_job_key = key;
            hands_job = std::async(std::launch::async, [data = document->data()] { return rate_hands(data); });
        }

        std::tuple<uint64_t, uint64_t, uint64_t, double> wanted = {key.first, key.second, hands.has_value() ? hands_key.second : ~0ull, playback.length()};

        if (wanted != findings_key) {
            findings = check_chart(document->data(), {playback.length(), document->directory(), hands, document->merged_on_open()});
            findings_key = wanted;
        }
    }

    void EditorGame::draw_checker_window() {
        if (!checker_open) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.4f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(CHECKER_WIDTH, 0), ImVec2(CHECKER_WIDTH, viewport->WorkSize.y * 0.8f));

        if (!ImGui::Begin("Check chart", &checker_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::End();
            return;
        }

        if (document == nullptr) {
            ImGui::TextDisabled("No chart open");
            ImGui::End();
            return;
        }

        static constexpr std::array<const char*, 4> TAGS = {"Problem", "Warning", "Info", "OK"};
        static const std::array<ImVec4, 4> COLORS = {ImVec4(1.0f, 0.45f, 0.45f, 1.0f), ImVec4(1.0f, 0.8f, 0.4f, 1.0f), ImVec4(0.6f, 0.75f, 1.0f, 1.0f), ImVec4(0.5f, 0.85f, 0.55f, 1.0f)};
        size_t problems = (size_t)std::count_if(findings.begin(), findings.end(), [](const CheckFinding& finding) { return finding.level == CheckLevel::PROBLEM; });
        size_t warnings = (size_t)std::count_if(findings.begin(), findings.end(), [](const CheckFinding& finding) { return finding.level == CheckLevel::WARNING; });
        std::string summary = problems + warnings == 0 ? std::string("Nothing to fix") : std::format("{} {}, {} {}", problems, problems == 1 ? "problem" : "problems", warnings, warnings == 1 ? "warning" : "warnings");
        ImGui::TextUnformatted(summary.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("(updates as you edit, click a time to go there)");
        ImGui::Separator();
        ImGui::PushTextWrapPos(CHECKER_WIDTH - 24);

        for (size_t index = 0; index < findings.size(); index++) {
            const CheckFinding& finding = findings[index];
            size_t level = (size_t)finding.level;
            ImGui::PushID((int)index);
            ImGui::TextColored(COLORS[level], "%s", TAGS[level]);
            ImGui::SameLine(80);

            if (finding.detail.empty() && finding.times.empty()) {
                ImGui::TextUnformatted(finding.title.c_str());
                ImGui::PopID();
                continue;
            }

            if (ImGui::TreeNodeEx("##finding", ImGuiTreeNodeFlags_SpanAvailWidth | (finding.level == CheckLevel::PROBLEM ? ImGuiTreeNodeFlags_DefaultOpen : 0), "%s", finding.title.c_str())) {
                if (!finding.detail.empty()) {
                    ImGui::TextDisabled("%s", finding.detail.c_str());
                }

                float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
                size_t listed = std::min(finding.times.size(), CHECKER_TIMES);

                for (size_t time = 0; time < listed; time++) {
                    std::string label = clock_text(finding.times[time]);

                    if (time > 0 && ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + ImGui::CalcTextSize(label.c_str()).x + ImGui::GetStyle().FramePadding.x * 2 < right) {
                        ImGui::SameLine();
                    }

                    ImGui::PushID((int)time);

                    if (ImGui::SmallButton(label.c_str())) {
                        int32_t at = finding.times[time];
                        seek(at);
                        selection.clear();
                        std::copy_if(document->data().hits.begin(), document->data().hits.end(), std::back_inserter(selection), [&](const Hit& hit) { return hit.time == at; });
                        selection_revision = document->revision();
                    }

                    ImGui::PopID();
                }

                if (finding.times.size() > listed) {
                    ImGui::TextDisabled("%s", std::format("and {} more", finding.times.size() - listed).c_str());
                }

                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        ImGui::PopTextWrapPos();
        ImGui::End();
    }

    void EditorGame::draw_pattern_settings(PatternFamily family) {
        PatternSettings& settings = pattern_settings;
        int keys = std::max<int>(document->data().info.key_count, 1);
        constexpr float WIDTH = 200.0f;

        auto lane_slider = [&](const char* label, uint8_t& lane) {
            int value = std::min<int>(lane, keys - 1) + 1;
            ImGui::SetNextItemWidth(WIDTH);

            if (ImGui::SliderInt(label, &value, 1, keys)) {
                lane = (uint8_t)(value - 1);
            }
        };

        auto lane_combo = [&](const char* label, int& lane) {
            bool changed = false;
            ImGui::SetNextItemWidth(WIDTH);

            if (ImGui::BeginCombo(label, std::format("Lane {}", lane + 1).c_str())) {
                for (int option = 0; option < keys; option++) {
                    if (ImGui::Selectable(std::format("Lane {}", option + 1).c_str(), option == lane)) {
                        lane = option;
                        changed = true;
                    }
                }

                ImGui::EndCombo();
            }

            return changed;
        };

        auto lane_range = [&]() {
            int from = std::min<int>(settings.first_lane, keys - 1);
            int to = std::min<int>(settings.last_lane, keys - 1);

            if (lane_combo("From lane", from)) {
                settings.first_lane = (uint8_t)from;
                settings.last_lane = to < from ? (uint8_t)from : settings.last_lane;
            }

            if (lane_combo("To lane", to)) {
                settings.last_lane = to == keys - 1 ? 255 : (uint8_t)to;
                settings.first_lane = from > to ? (uint8_t)to : settings.first_lane;
            }
        };

        auto percent = [&](const char* label, int& value, int minimum) {
            ImGui::SetNextItemWidth(WIDTH);
            ImGui::SliderInt(label, &value, minimum, 100, "%d%%");
        };

        auto choice = [&](const char* label, auto& value, std::initializer_list<const char*> names) {
            std::vector<const char*> list(names);
            int index = std::clamp((int)value, 0, (int)list.size() - 1);
            ImGui::SetNextItemWidth(WIDTH);

            if (ImGui::Combo(label, &index, list.data(), (int)list.size())) {
                value = (std::remove_reference_t<decltype(value)>)index;
            }
        };

        switch (family) {
            case PatternFamily::STREAM: {
                lane_range();
                choice("Flow", settings.flow, {"Random", "Roll", "Zigzag"});

                if (settings.flow == StreamFlow::RANDOM) {
                    percent("Disconnect chance", settings.disconnect_chance, 0);
                    percent("Trill chance", settings.trill_chance, 0);
                }
                else {
                    ImGui::Checkbox("Reverse direction", &settings.reverse);
                }

                choice("Chords", settings.chords, {"None", "Jumps", "Hands", "Quads", "Custom size"});

                if (settings.chords != StreamChords::NONE) {
                    ImGui::SetNextItemWidth(WIDTH);
                    ImGui::SliderInt("Chord every", &settings.chord_every, 1, 16, settings.chord_every == 1 ? "note" : "%d notes");

                    if (settings.chords == StreamChords::CUSTOM) {
                        ImGui::SetNextItemWidth(WIDTH);
                        ImGui::SliderInt("Chord size", &settings.chord_size, 2, std::max(2, keys));
                    }

                    if (stream_chord_size(settings) > 2) {
                        ImGui::Checkbox("Jumps between chords", &settings.jumps_between);
                    }

                    if ((int)stream_chord_size(settings) > keys) {
                        ImGui::TextDisabled("Chords are capped at %d notes, the number of lanes", keys);
                    }
                }

                percent("Jack chance", settings.jack_chance, 0);
                ImGui::Checkbox("Anchor", &settings.anchor);

                if (settings.anchor) {
                    lane_slider("Anchor lane", settings.anchor_lane);
                    ImGui::SetNextItemWidth(WIDTH);
                    ImGui::SliderInt("Anchor every", &settings.anchor_every, 2, 8, "%d notes");
                    ImGui::Checkbox("Keep the anchor lane out of the stream", &settings.anchor_alone);
                }

                break;
            }
            case PatternFamily::SINGLE_TRILL:
                lane_slider("Lane A", settings.lane_a);
                lane_slider("Lane B", settings.lane_b);
                break;
            case PatternFamily::SPLIT_TRILL:
                lane_range();
                choice("Split", settings.split, {"Odd / even lanes (13 24)", "Left / right halves (12 34)", "Inner / outer (14 23)"});
                ImGui::Checkbox("Start on the other group", &settings.reverse);
                break;
            case PatternFamily::JACKS:
                lane_range();
                percent("Density", settings.density, 1);
                ImGui::Checkbox("Vary chord sizes", &settings.vary_sizes);
                ImGui::SetNextItemWidth(WIDTH);
                ImGui::SliderInt("Lanes kept", &settings.kept_lanes, 1, keys);

                break;
        }

        settings.chord_every = std::clamp(settings.chord_every, 1, 16);
        settings.chord_size = std::clamp(settings.chord_size, 2, std::max(2, keys));
        settings.anchor_every = std::clamp(settings.anchor_every, 2, 8);
        settings.kept_lanes = std::clamp(settings.kept_lanes, 1, keys);
    }

    void EditorGame::draw_pattern_window() {
        pattern_preview.clear();

        if (!pattern_open || document == nullptr) {
            return;
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + viewport->WorkSize.y * 0.3f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin("Generate pattern", &pattern_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) {
            ImGui::End();
            return;
        }

        const std::vector<PatternStyle>& styles = pattern_styles();
        pattern_style = std::min(pattern_style, styles.size() - 1);
        ImGui::SetNextItemWidth(ui_px(200));

        if (ImGui::BeginCombo("Style", styles[pattern_style].name)) {
            for (size_t index = 0; index < styles.size(); index++) {
                if (ImGui::Selectable(styles[index].name, index == pattern_style)) {
                    pattern_style = index;
                    pattern_variant = 0;
                }
            }

            ImGui::EndCombo();
        }

        const std::vector<PatternVariant>& variants = styles[pattern_style].variants;
        pattern_variant = std::min(pattern_variant, variants.size() - 1);

        if (variants.size() > 1) {
            ImGui::SetNextItemWidth(ui_px(200));

            if (ImGui::BeginCombo("Pattern", variants[pattern_variant].name)) {
                for (size_t index = 0; index < variants.size(); index++) {
                    if (ImGui::Selectable(variants[index].name, index == pattern_variant)) {
                        pattern_variant = index;
                    }
                }

                ImGui::EndCombo();
            }
        }

        ImGui::SeparatorText("Pattern");
        draw_pattern_settings(variants[pattern_variant].family);
        ImGui::SeparatorText("Placement");
        ImGui::SetNextItemWidth(ui_px(200));
        snap_picker("Snap", pattern_divisor);
        ImGui::SetNextItemWidth(ui_px(200));
        ImGui::InputInt("Beats", &pattern_beats);
        pattern_beats = std::clamp(pattern_beats, 1, MAX_PATTERN_BEATS);
        ImGui::Checkbox("Replace notes already there", &pattern_replace);

        if (ImGui::Button("New random pattern")) {
            pattern_seed = std::random_device{}();
        }

        ImGui::Separator();

        if (document->data().timing_points.empty()) {
            ImGui::TextUnformatted("Add a timing point first, patterns are placed on the beat grid.");
            ImGui::End();
            return;
        }

        pattern_preview = pattern_hits();

        if (!pattern_preview.empty()) {
            int32_t first = pattern_preview.front().time;
            int32_t last = pattern_preview.back().time;
            size_t replaced = pattern_replace ? (size_t)std::count_if(document->data().hits.begin(), document->data().hits.end(), [&](const Hit& hit) { return hit.time >= first && hit.time <= last; }) : 0;
            ImGui::TextUnformatted(std::format("{} notes from {} to {}", pattern_preview.size(), clock_text(first), clock_text(last)).c_str());

            if (replaced > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", std::format("Replaces the {} notes already in that stretch", replaced).c_str());
            }
        }

        if (ImGui::Button("Place pattern", ImVec2(ui_px(140), 0))) {
            place_pattern();
        }

        ImGui::End();
    }

    void EditorGame::draw_quantize_popup() {
        if (quantize_requested) {
            ImGui::OpenPopup("Quantize selection");
            quantize_requested = false;
        }

        if (!ImGui::BeginPopupModal("Quantize selection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        prune_selection();

        if (document == nullptr || selection.empty()) {
            ImGui::TextUnformatted("Nothing is selected anymore.");
        }
        else if (document->data().timing_points.empty()) {
            ImGui::TextUnformatted("Add a timing point first, there's no beat grid to quantize to.");
        }
        else {
            ImGui::SetNextItemWidth(ui_px(120));
            snap_picker("Snap to", quantize_divisor);

            ImGui::Checkbox("Quantize long note ends too", &quantize_ends);
            std::vector<Hit> result = document->quantized(selection, quantize_divisor, quantize_ends);
            size_t moved = 0;
            int32_t largest = 0;
            std::set<std::pair<int32_t, uint8_t>> spots;

            for (size_t index = 0; index < selection.size(); index++) {
                int32_t shift = std::max(std::abs(result[index].time - selection[index].time), std::abs(result[index].end_time - selection[index].end_time));
                moved += shift != 0 ? 1 : 0;
                largest = std::max(largest, shift);
                spots.insert({result[index].time, result[index].key});
            }

            size_t merged = selection.size() - spots.size();
            ImGui::Spacing();

            if (moved == 0) {
                ImGui::TextUnformatted(std::format("All {} selected notes are already on 1/{}.", selection.size(), quantize_divisor).c_str());
            }
            else {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "%s", std::format("{} of {} selected notes will move, by up to {} ms.", moved, selection.size(), largest).c_str());
            }

            if (merged > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", std::format("{} notes will land on the same lane and time as another and be merged.", merged).c_str());
            }

            ImGui::Spacing();
            ImGui::BeginDisabled(moved == 0);

            if (ImGui::Button("Quantize", ImVec2(ui_px(110), 0))) {
                document->change([&](ChartData& data) { replace_hits(data, selection, result); });
                selection.clear();

                for (const Hit& hit : result) {
                    if (document->contains(hit) && !is_selected(hit)) {
                        selection.push_back(hit);
                    }
                }

                selection_revision = document->revision();
                set_status(std::format("Quantized {} notes to 1/{}", moved, quantize_divisor));
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndDisabled();
            ImGui::SameLine();
        }

        if (ImGui::Button("Cancel", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorGame::draw_transform_popup() {
        if (transform_requested) {
            ImGui::OpenPopup("Transform selection");
            transform_requested = false;
        }

        if (!ImGui::BeginPopupModal("Transform selection", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        prune_selection();

        if (document == nullptr || selection.empty()) {
            ImGui::TextUnformatted("Nothing is selected anymore.");
        }
        else {
            auto [first, last] = std::minmax_element(selection.begin(), selection.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; });
            int32_t end = last->time;

            for (const Hit& hit : selection) {
                end = std::max(end, hit.hit_type == HitType::HOLD ? hit.end_time : hit.time);
            }

            ImGui::TextUnformatted(std::format("{} notes selected, {} to {}", selection.size(), clock_text(first->time), clock_text(end)).c_str());
            struct SnapChange {
                uint32_t from;
                uint32_t to;
            };

            constexpr SnapChange STRETCHES[] = {{8, 4}, {16, 4}, {32, 4}, {6, 4}, {8, 6}, {12, 4}};
            constexpr SnapChange SHRINKS[] = {{4, 8}, {4, 16}, {4, 32}, {4, 6}, {6, 8}, {4, 12}};

            auto change_buttons = [&](const char* title, const SnapChange* changes, size_t count) {
                ImGui::SeparatorText(title);

                for (size_t index = 0; index < count; index++) {
                    const SnapChange& change = changes[index];
                    uint32_t common = std::gcd(change.from, change.to);
                    uint32_t numerator = change.from / common;
                    uint32_t denominator = change.to / common;
                    std::string factor = denominator == 1 ? std::format("{}x", numerator) : std::format("{}/{}x", numerator, denominator);

                    if (index != 0) {
                        ImGui::SameLine();
                    }

                    if (ImGui::Button(std::format("{}##{}", factor, title).c_str(), ImVec2(ui_px(70), 0))) {
                        stretch_selection((double)change.from / (double)change.to, transform_from_playhead);
                    }

                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Turns 1/%u snaps into 1/%u", change.from, change.to);
                    }
                }
            };

            if (ImGui::RadioButton("Keep the first note in place", !transform_from_playhead)) {
                transform_from_playhead = false;
            }

            ImGui::SameLine();

            if (ImGui::RadioButton("Keep the playhead in place", transform_from_playhead)) {
                transform_from_playhead = true;
            }

            change_buttons("Stretch (spread the notes out)", STRETCHES, std::size(STRETCHES));
            change_buttons("Shrink (pack the notes closer)", SHRINKS, std::size(SHRINKS));

            ImGui::SeparatorText("Lanes");

            if (ImGui::Button("Mirror (M)", ImVec2(ui_px(110), 0))) {
                mirror_selection();
            }

            int key_count = std::max<int>(document->data().info.key_count, 2);
            ImGui::SetNextItemWidth(ui_px(110));
            ImGui::InputInt("Columns", &rotate_columns);
            rotate_columns = std::clamp(rotate_columns, 1, key_count - 1);
            ImGui::SameLine();

            if (ImGui::Button("Rotate left", ImVec2(ui_px(110), 0))) {
                rotate_selection(-rotate_columns);
            }

            ImGui::SameLine();

            if (ImGui::Button("Rotate right", ImVec2(ui_px(110), 0))) {
                rotate_selection(rotate_columns);
            }
        }

        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(ui_px(110), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void EditorGame::rotate_selection(int32_t columns) {
        prune_selection();

        if (document == nullptr || selection.empty()) {
            return;
        }

        std::vector<Hit> rotated = rotated_hits(selection, document->data().info.key_count, columns);
        document->change([&](ChartData& data) { replace_hits(data, selection, rotated); });
        selection = std::move(rotated);
        selection_revision = document->revision();
        set_status(std::format("Rotated {} notes {} by {}", selection.size(), columns < 0 ? "left" : "right", std::abs(columns)));
    }

    void EditorGame::select_all() {
        if (document == nullptr) {
            return;
        }

        selection = document->data().hits;
        selection_revision = document->revision();
        set_status(std::format("Selected {} notes", selection.size()));
    }

    void EditorGame::copy_selection() {
        prune_selection();

        if (selection.empty()) {
            set_status("Select some notes first (Shift + drag, or Ctrl + A)", true);
            return;
        }

        int32_t earliest = std::min_element(selection.begin(), selection.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; })->time;
        clipboard = shifted_hits(selection, -earliest, 0);
        set_status(std::format("Copied {} notes", clipboard.size()));
    }

    void EditorGame::cut_selection() {
        prune_selection();

        if (selection.empty()) {
            set_status("Select some notes first (Shift + drag, or Ctrl + A)", true);
            return;
        }

        copy_selection();
        document->change([&](ChartData& data) { replace_hits(data, selection, {}); });
        selection.clear();
        set_status(std::format("Cut {} notes", clipboard.size()));
    }

    void EditorGame::paste_clipboard() {
        if (document == nullptr || clipboard.empty()) {
            set_status("Nothing to paste yet, copy some notes first", true);
            return;
        }

        int32_t at = (int32_t)std::lround(place_time(position));
        uint8_t key_count = document->data().info.key_count;
        std::vector<Hit> pasted;

        for (const Hit& hit : shifted_hits(clipboard, at, 0)) {
            if (hit.key < key_count) {
                pasted.push_back(hit);
            }
        }

        if (pasted.empty()) {
            set_status(std::format("Those notes are all past lane {}", key_count), true);
            return;
        }

        document->change([&](ChartData& data) { replace_hits(data, {}, pasted); });
        size_t skipped = clipboard.size() - pasted.size();
        selection = std::move(pasted);
        selection_revision = document->revision();
        set_status(skipped > 0 ? std::format("Pasted {} notes, left out {} past lane {}", selection.size(), skipped, key_count) : std::format("Pasted {} notes at {}", selection.size(), clock_text(at)));
    }

    void EditorGame::delete_selection() {
        prune_selection();

        if (document == nullptr || selection.empty()) {
            return;
        }

        size_t count = selection.size();
        document->change([&](ChartData& data) { replace_hits(data, selection, {}); });
        selection.clear();
        set_status(std::format("Deleted {} notes", count));
    }

    void EditorGame::collect_grid() {
        grid.clear();
        markers.clear();

        if (field == nullptr || document == nullptr || skin == nullptr || document->data().timing_points.empty()) {
            return;
        }

        double top = field->time_at(view.y, window);
        double bottom = std::max(0.0, field->time_at(view.y + view.h, window));
        double time = document->snap(bottom, divisor);
        float half = (float)(field->note_height() / 2);
        float right = (float)(field->pos.x + field->size.x);
        ImDrawList* labels = ImGui::GetBackgroundDrawList();

        if (time < bottom - 0.5) {
            time = document->step(time, divisor, 1);
        }

        for (size_t count = 0; count < GRID_LIMIT && time <= top; count++) {
            const ChartTimingPoint* point = document->timing_point_at(time);
            double length = point->beat_length / (double)divisor;
            int64_t tick = (int64_t)std::llround((time - point->time) / length);
            int64_t within = ((tick % divisor) + divisor) % divisor;
            int64_t beat = (tick - within) / (int64_t)divisor;
            int64_t measure_beat = ((beat % point->meter) + point->meter) % point->meter;
            float y = std::floor((float)field->note_top(time, window) + half);
            GridLine line;
            line.y = y;
            line.height = 2;

            if (within == 0 && measure_beat == 0) {
                line.color = olc::Pixel(255, 255, 255, 180);
                line.height = 3;
                line.measure = (int32_t)(beat / point->meter + 1);
            }
            else if (within == 0) {
                line.color = olc::Pixel(255, 255, 255, 105);
            }
            else {
                line.color = faded(skin->color((SkinColor)((size_t)SkinColor::SNAP_4TH + snap_index((uint32_t)within, divisor))), 95);
            }

            if (y >= view.y && y <= view.y + view.h) {
                grid.push_back(line);

                if (line.measure >= 0) {
                    std::string label = std::to_string(line.measure);
                    ImVec2 size = ImGui::CalcTextSize(label.c_str());
                    labels->AddText(ImVec2((float)field->pos.x - size.x - ui_px(8.0f), y - size.y / 2), IM_COL32(255, 255, 255, 170), label.c_str());
                }
            }

            double next = document->step(time, divisor, 1);

            if (next <= time + 0.01) {
                break;
            }

            time = next;
        }

        for (const ChartTimingPoint& point : document->data().timing_points) {
            if (point.time < bottom - 1.0 || point.time > top + 1.0) {
                continue;
            }

            float y = std::floor((float)field->note_top(point.time, window) + half);
            std::string label = std::format("{:.2f} BPM  {}/4", 60000.0 / point.beat_length, point.meter);
            ImVec2 size = ImGui::CalcTextSize(label.c_str());
            ImVec2 corner(right + ui_px(10.0f), y - size.y / 2);
            labels->AddRectFilled(ImVec2(corner.x - 4.0f, corner.y - 2.0f), ImVec2(corner.x + size.x + 4.0f, corner.y + size.y + 2.0f), IM_COL32(40, 22, 12, 200), 3.0f);
            labels->AddText(corner, IM_COL32(255, 170, 90, 255), label.c_str());
            markers.push_back({y, std::move(label)});
        }
    }

    double EditorGame::timeline_length() const {
        double length = playback.length();

        if (document != nullptr) {
            for (const Hit& hit : document->data().hits) {
                length = std::max(length, (double)(hit.hit_type == HitType::HOLD ? hit.end_time : hit.time) + TIMELINE_TAIL_MS);
            }
        }

        return std::max(length, TIMELINE_TAIL_MS);
    }

    void EditorGame::update_density(size_t rows, double length) {
        if (density_revision == document->revision() && density.size() == rows && density_length == length) {
            return;
        }

        density.assign(rows, 0.0f);
        density_revision = document->revision();
        density_length = length;

        for (const Hit& hit : document->data().hits) {
            size_t row = (size_t)std::clamp(hit.time / length * (double)rows, 0.0, (double)rows - 1);
            density[row] += 1.0f;
        }

        double row_seconds = length / (double)rows / 1000.0;

        for (float& value : density) {
            value = (float)(value / row_seconds);
        }

        std::vector<float> smoothed = density;

        for (size_t row = 0; row < rows; row++) {
            float total = 0;
            int count = 0;

            for (size_t other = row > 1 ? row - 2 : 0; other <= row + 2 && other < rows; other++) {
                total += density[other];
                count++;
            }

            smoothed[row] = total / (float)std::max(1, count);
        }

        density = std::move(smoothed);
        density_peak = std::max(1.0f, *std::max_element(density.begin(), density.end()));
    }

    void EditorGame::draw_density_bar() {
        if (document == nullptr || view.h < 80.0f) {
            return;
        }

        float bar_width = ui_px(DENSITY_BAR_WIDTH);
        float margin = ui_px(DENSITY_BAR_MARGIN);
        float height = view.h - margin * 2;
        ImVec2 corner(view.x + view.w - bar_width - margin, view.y + margin);
        size_t rows = (size_t)std::max(1.0f, height / DENSITY_ROW_PX);
        double length = timeline_length();
        update_density(rows, length);

        ImGui::SetNextWindowPos(corner);
        ImGui::SetNextWindowSize(ImVec2(bar_width, height));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##density_bar", nullptr, flags)) {
            ImGui::InvisibleButton("##density_seek", ImVec2(bar_width, height));
            bool hovered = ImGui::IsItemHovered();
            bool active = ImGui::IsItemActive();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            float bottom = corner.y + height;
            auto y_of = [&](double time) {
                return bottom - (float)(std::clamp(time / length, 0.0, 1.0) * height);
            };
            auto time_of = [&](float y) {
                return std::clamp((double)(bottom - y) / height, 0.0, 1.0) * length;
            };

            draw->AddRectFilled(corner, ImVec2(corner.x + bar_width, bottom), IM_COL32(12, 12, 18, 200), 4.0f);

            for (size_t row = 0; row < rows; row++) {
                float level = density[row] / density_peak;

                if (level <= 0.01f) {
                    continue;
                }

                ImVec4 low(0.35f, 0.62f, 1.0f, 0.9f);
                ImVec4 mid(1.0f, 0.82f, 0.31f, 0.9f);
                ImVec4 high(1.0f, 0.32f, 0.32f, 0.95f);
                float t = std::clamp(level, 0.0f, 1.0f);
                ImVec4 color = t < 0.5f
                    ? ImVec4(low.x + (mid.x - low.x) * t * 2, low.y + (mid.y - low.y) * t * 2, low.z + (mid.z - low.z) * t * 2, low.w)
                    : ImVec4(mid.x + (high.x - mid.x) * (t - 0.5f) * 2, mid.y + (high.y - mid.y) * (t - 0.5f) * 2, mid.z + (high.z - mid.z) * (t - 0.5f) * 2, high.w);
                float width = (bar_width - 6.0f) * t;
                float y = bottom - (float)(row + 1) * DENSITY_ROW_PX;
                draw->AddRectFilled(ImVec2(corner.x + 3.0f, y), ImVec2(corner.x + 3.0f + width, y + DENSITY_ROW_PX), ImGui::ColorConvertFloat4ToU32(color));
            }

            for (const ChartTimingPoint& point : document->data().timing_points) {
                float y = y_of(point.time);
                draw->AddLine(ImVec2(corner.x + bar_width - 8.0f, y), ImVec2(corner.x + bar_width, y), IM_COL32(255, 140, 60, 230), 2.0f);
            }

            if (field != nullptr) {
                float top = y_of(field->time_at(view.y, window));
                float low = y_of(std::max(0.0, field->time_at(view.y + view.h, window)));
                draw->AddRectFilled(ImVec2(corner.x, top), ImVec2(corner.x + bar_width, low), IM_COL32(255, 255, 255, 28));
                draw->AddRect(ImVec2(corner.x, top), ImVec2(corner.x + bar_width, low), IM_COL32(255, 255, 255, 70));
            }

            float playhead = y_of(position);
            draw->AddLine(ImVec2(corner.x - 3.0f, playhead), ImVec2(corner.x + bar_width + 3.0f, playhead), IM_COL32(255, 255, 255, 255), 2.0f);
            draw->AddRect(corner, ImVec2(corner.x + bar_width, bottom), IM_COL32(255, 255, 255, hovered || active ? 120 : 50), 4.0f);

            float mouse_y = ImGui::GetIO().MousePos.y;

            if (active) {
                seek(time_of(mouse_y));
            }

            if (hovered || active) {
                size_t row = (size_t)std::clamp((double)(bottom - mouse_y) / DENSITY_ROW_PX, 0.0, (double)rows - 1);
                ImGui::SetTooltip("%s  %.1f notes/s", clock_text(time_of(mouse_y)).c_str(), density[row]);
            }
        }

        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void EditorGame::draw_waveform(olc::PixelGameEngine* target) {
        if (waveform_opacity <= 0.0f || !waveform.ready()) {
            return;
        }

        uint8_t alpha = (uint8_t)std::lround(std::clamp(waveform_opacity, 0.0f, 1.0f) * 255.0f);
        const std::array<olc::Pixel, WaveformBands::BAND_COUNT> COLORS = {
            olc::Pixel(255, 86, 64, alpha),
            olc::Pixel(255, 206, 84, alpha),
            olc::Pixel(90, 176, 255, alpha),
        };

        float left = (float)field->pos.x;
        float width = (float)field->size.x;
        float center = left + width / 2;
        float top = (float)field->pos.y;
        float bottom = (float)(field->pos.y + field->size.y);

        for (float y = top; y < bottom; y += WAVEFORM_ROW_PX) {
            double newer = field->time_at(y, target);
            double older = field->time_at(y + WAVEFORM_ROW_PX, target);
            std::array<float, WaveformBands::BAND_COUNT> levels = waveform.peak(older, newer);

            for (size_t band = 0; band < WaveformBands::BAND_COUNT; band++) {
                float bar = std::min(1.0f, levels[band]) * width;

                if (bar >= 1.0f) {
                    target->FillRectDecal({center - bar / 2, y}, {bar, WAVEFORM_ROW_PX}, COLORS[band]);
                }
            }
        }
    }

    void EditorGame::draw_underlay(olc::PixelGameEngine* target) {
        draw_waveform(target);
        float left = (float)field->pos.x;
        float width = (float)field->size.x;

        for (const GridLine& line : grid) {
            target->FillRectDecal({left, line.y - std::floor(line.height / 2)}, {width, line.height}, line.color);
        }

        for (const TimingMarker& marker : markers) {
            target->FillRectDecal({left, marker.y - 1}, {width, 3}, olc::Pixel(255, 140, 60, 220));
        }
    }

    void EditorGame::draw_overlays(olc::PixelGameEngine* target) {
        uint8_t keys = preview->info.key_count;
        float height = (float)field->note_height();
        float width = (float)field->lane_width();
        bool on_grid = std::abs(document->snap(position, divisor) - position) < ON_GRID_MS;
        float judge_y = std::floor((float)field->note_top(position, target) + height / 2);
        target->FillRectDecal({(float)field->pos.x - JUDGE_LINE_OVERHANG, judge_y - 1}, {(float)field->size.x + JUDGE_LINE_OVERHANG * 2, 2}, on_grid ? olc::Pixel(90, 230, 255, 235) : olc::Pixel(255, 80, 120, 235));

        if (!snap_enabled && (gesture.kind == Gesture::Kind::PLACE || (show_ghost && hover_column.has_value()))) {
            double placing = gesture.kind == Gesture::Kind::PLACE ? gesture.end : hover_time;
            float y = std::floor((float)field->note_top(placing, target) + height / 2);
            target->FillRectDecal({(float)field->pos.x, y}, {(float)field->size.x, 1}, olc::Pixel(255, 255, 255, 200));
        }

        for (const Hit& hit : pattern_preview) {
            float top = (float)field->note_top(hit.time, target);
            float tail = hit.hit_type == HitType::HOLD ? (float)field->note_top(hit.end_time, target) : top;

            if (hit.key >= keys || top + height < view.y || tail > view.y + view.h) {
                continue;
            }

            float x = (float)field->lane_left(hit.key);
            olc::Decal* decal = field->sized_image(SkinImage::NOTE, hit.key, keys);

            if (hit.hit_type == HitType::HOLD) {
                target->FillRectDecal({x + width * 0.2f, tail + height / 2}, {width * 0.6f, top - tail}, olc::Pixel(255, 255, 255, 60));
                field->draw_note(target, decal, x, tail, olc::Pixel(255, 255, 255, 90));
            }

            field->draw_note(target, decal, x, top, olc::Pixel(255, 255, 255, 115));
        }

        if (show_ghost && hover_column.has_value()) {
            olc::Decal* decal = field->sized_image(SkinImage::NOTE, *hover_column, keys);
            field->draw_note(target, decal, field->lane_left(*hover_column), field->note_top(hover_time, target), olc::Pixel(255, 255, 255, 110));
        }

        if (gesture.kind == Gesture::Kind::PLACE) {
            double start = std::min(gesture.start, gesture.end);
            double end = std::max(gesture.start, gesture.end);
            float x = (float)field->lane_left(gesture.key);
            float head = (float)field->note_top(start, target);
            float tail = (float)field->note_top(end, target);
            olc::Decal* decal = field->sized_image(SkinImage::NOTE, gesture.key, keys);

            if (end > start) {
                target->FillRectDecal({x + width * 0.2f, tail + height / 2}, {width * 0.6f, head - tail}, olc::Pixel(255, 255, 255, 80));
                field->draw_note(target, decal, x, tail, olc::Pixel(255, 255, 255, 140));
            }

            field->draw_note(target, decal, x, head, olc::Pixel(255, 255, 255, 170));
        }

        for (const Hit& hit : selection) {
            if (hit.key >= keys) {
                continue;
            }

            float x = (float)field->lane_left(hit.key);
            float head = (float)field->note_top(hit.time, target);
            float tail = hit.hit_type == HitType::HOLD ? (float)field->note_top(hit.end_time, target) : head;

            if (head + height < view.y || tail > view.y + view.h) {
                continue;
            }

            outline(target, x - 2, tail - 2, width + 4, head - tail + height + 4, olc::Pixel(255, 214, 92, 230), 2.0f);
        }

        if (gesture.kind == Gesture::Kind::SELECT) {
            uint8_t first = std::min(gesture.key, gesture.end_key);
            uint8_t last = std::max(gesture.key, gesture.end_key);
            float left = (float)field->lane_left(first) - 2;
            float right = (float)field->lane_left(last) + width + 2;
            float top = (float)field->note_top(std::max(gesture.start, gesture.end), target);
            float bottom = (float)field->note_top(std::min(gesture.start, gesture.end), target) + height;
            target->FillRectDecal({left, top}, {right - left, bottom - top}, olc::Pixel(120, 190, 255, 45));
            outline(target, left, top, right - left, bottom - top, olc::Pixel(120, 190, 255, 200), 1.0f);
        }
    }

    void EditorGame::draw(olc::PixelGameEngine* target) {
        target->SetDrawTarget(EDITOR_LAYER);
        olc::vi2d screen = target->GetScreenSize();
        olc::Pixel background = skin != nullptr ? skin->color(SkinColor::SCREEN_BACKGROUND) : olc::Pixel(16, 16, 20);
        background.a = 255;
        target->FillRectDecal({0, 0}, {(float)screen.x, (float)screen.y}, background);

        if (field == nullptr || preview == nullptr) {
            return;
        }

        SkinLayoutScope layout_scope(skin.get(), preview->info.key_count);
        field->draw(target);
        draw_overlays(target);
    }

    void EditorGame::render_interface() {
        imgui.render();
    }
}
