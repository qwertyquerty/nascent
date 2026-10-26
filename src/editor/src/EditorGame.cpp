#include "EditorGame.h"

#include "ArchiveExport.h"
#include "DifficultyCalculator.h"
#include "OsuExport.h"
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

namespace nascent {
    namespace {
        constexpr double STATUS_SECONDS = 6.0;
        constexpr double DIFFICULTY_DELAY_MS = 400.0;
        constexpr double HITSOUND_JUMP_MS = 250.0;
        constexpr size_t HITSOUNDS_PER_FRAME = 8;
        constexpr size_t GRID_LIMIT = 4000;
        constexpr uint32_t MIN_SCROLL_SPEED = 50;
        constexpr uint32_t MAX_SCROLL_SPEED = 500;
        constexpr int32_t SCROLL_SPEED_STEP = 10;
        constexpr size_t METRONOME_PER_FRAME = 4;
        constexpr float WAVEFORM_ROW_PX = 3.0f;
        constexpr double WAVEFORM_VISUAL_OFFSET_MS = 20.0;
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
        playback.unload();
        waveform.clear();
        field.reset();
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
        input().start();
        use_blank_canvas();
        window->GetLayers()[EDITOR_LAYER].pDrawTarget.Create(1, 1);
        window->EnableLayer(EDITOR_LAYER, true);
        Ui::set_screen(window->GetScreenSize());
        load_skin(Settings().skin);
        imgui.init(settings_directory / "editor_layout.ini", std::filesystem::path("assets") / "fonts" / "MPLUS1-Regular.ttf", FONT_SIZE);
        allow_window_maximize();
        load_metronome();
        load_recent();
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
        document = std::move(next);
        selection.clear();
        selection_revision = ~0ull;
        gesture = {};
        position = 0;
        hitsound_position = 0;
        preview_revision = ~0ull;
        difficulty.reset();
        difficulty_revision = ~0ull;
        changed_revision = ~0ull;
        reload_audio();
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

        replace_document(std::move(next));
        remember(path);
        set_status(document->path().empty() ? std::format("Opened {}, saving makes a Nascent .json copy next to it", path_utf8(path.filename())) : std::format("Opened {}", path_utf8(path.filename())));
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

        replace_document(std::move(next));
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
        if (document != nullptr && document->dirty()) {
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
            case Pending::NONE:
                return;
        }
    }

    bool EditorGame::can_close() {
        if (quit_confirmed || document == nullptr || !document->dirty()) {
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
        auto current = std::find(DIVISORS.begin(), DIVISORS.end(), divisor);
        int32_t index = current == DIVISORS.end() ? 3 : (int32_t)(current - DIVISORS.begin());
        divisor = DIVISORS[(size_t)std::clamp<int32_t>(index + (direction > 0 ? 1 : -1), 0, (int32_t)DIVISORS.size() - 1)];
    }

    void EditorGame::change_scroll_speed(int32_t notches) {
        scroll_speed = (uint32_t)std::clamp<int32_t>((int32_t)scroll_speed + notches * SCROLL_SPEED_STEP, (int32_t)MIN_SCROLL_SPEED, (int32_t)MAX_SCROLL_SPEED);
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
            preview.reset();
            preview_revision = ~0ull;
            return;
        }

        if (preview_revision == document->revision() && field != nullptr) {
            return;
        }

        field.reset();
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
            difficulty = difficulty_job.get();
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

        if (!layout_ready) {
            if (ImGui::DockBuilderGetNode(dockspace) == nullptr) {
                build_layout(dockspace);
            }

            layout_ready = true;
        }

        ImGui::DockSpaceOverViewport(dockspace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

        if (ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace); central != nullptr) {
            view = {central->Pos.x, central->Pos.y, central->Size.x, central->Size.y};
        }
        else {
            view = {viewport->WorkPos.x, viewport->WorkPos.y, viewport->WorkSize.x, viewport->WorkSize.y};
        }

        draw_transport();
        draw_chart_panel();
        draw_timing_panel();
        draw_help_panel();
        draw_dialogs();

        update_preview();
        layout_field();
        handle_field_input();
        handle_shortcuts();
        collect_grid();
        draw_density_bar();
        update_difficulty();

        imgui.end_frame();
    }

    void EditorGame::build_layout(unsigned int dockspace) {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderRemoveNode(dockspace);
        ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

        ImGuiID center = dockspace;
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
        ImGuiID left_bottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.42f, nullptr, &left);
        ImGuiID right_bottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.55f, nullptr, &right);

        ImGui::DockBuilderDockWindow("Chart", left);
        ImGui::DockBuilderDockWindow("Timing", left_bottom);
        ImGui::DockBuilderDockWindow("Transport", right);
        ImGui::DockBuilderDockWindow("Help", right_bottom);
        ImGui::DockBuilderFinish(dockspace);
    }

    void EditorGame::draw_menu() {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        bool open = document != nullptr;

        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New from audio...", "Ctrl+N")) {
                request(Pending::NEW_FROM_AUDIO);
            }

            if (ImGui::MenuItem("Open...", "Ctrl+O")) {
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

            if (ImGui::MenuItem("Save", "Ctrl+S", false, open)) {
                save();
            }

            if (ImGui::MenuItem("Save as...", "Ctrl+Shift+S", false, open)) {
                save_as();
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

            if (ImGui::MenuItem("Quit", "Ctrl+Q")) {
                request(Pending::QUIT);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, open && document->can_undo())) {
                document->undo();
            }

            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, open && document->can_redo())) {
                document->redo();
            }

            ImGui::Separator();

            bool any = open && !selection.empty();

            if (ImGui::MenuItem("Select all", "Ctrl+A", false, open)) {
                select_all();
            }

            if (ImGui::MenuItem("Cut", "Ctrl+X", false, any)) {
                cut_selection();
            }

            if (ImGui::MenuItem("Copy", "Ctrl+C", false, any)) {
                copy_selection();
            }

            if (ImGui::MenuItem("Paste at playhead", "Ctrl+V", false, open && !clipboard.empty())) {
                paste_clipboard();
            }

            if (ImGui::MenuItem("Mirror selected notes", "M", false, any)) {
                mirror_selection();
            }

            if (ImGui::MenuItem("Delete selected notes", "Del", false, any)) {
                delete_selection();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Transform selection...", "Ctrl+T", false, open && !selection.empty())) {
                transform_requested = true;
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

            ImGui::MenuItem("Hitsounds", nullptr, &hitsounds);
            ImGui::MenuItem("Metronome", nullptr, &metronome);

            if (ImGui::MenuItem("Snap colors", nullptr, &snap_colors) && field != nullptr) {
                field->snap_colors = snap_colors;
            }

            if (ImGui::MenuItem("Reset panel layout")) {
                build_layout(ImGui::GetID("editor_dockspace"));
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
            if (ImGui::Button(playback.is_playing() ? "Pause" : "Play", ImVec2(90, 0))) {
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

        if (ImGui::BeginCombo("Snap", std::format("1/{}", divisor).c_str())) {
            for (uint32_t option : DIVISORS) {
                if (ImGui::Selectable(std::format("1/{}", option).c_str(), option == divisor)) {
                    divisor = option;
                }
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

        ImGui::SeparatorText("Metadata");
        text_field("Title", &ChartInfo::title);
        text_field("Title (unicode)", &ChartInfo::title_unicode);
        text_field("Artist", &ChartInfo::artist);
        text_field("Artist (unicode)", &ChartInfo::artist_unicode);
        text_field("Mapper", &ChartInfo::mapper);
        text_field("Difficulty", &ChartInfo::difficulty_name);

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

        if (ImGui::Button("Add at playhead")) {
            const ChartTimingPoint* current = document->timing_point_at(position);
            ChartTimingPoint point = current != nullptr ? *current : ChartTimingPoint{0, 60000.0f / ChartDocument::DEFAULT_BPM, 4};
            point.time = (int32_t)std::lround(position);
            document->change([&](ChartData& edited) { edited.timing_points.push_back(point); });
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

        if (ImGui::BeginTable("##timing", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
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

        if (points.empty()) {
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

        if (ImGui::Button("Tap", ImVec2(110, 36))) {
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

    void EditorGame::draw_help_panel() {
        if (!ImGui::Begin("Help")) {
            ImGui::End();
            return;
        }

        constexpr std::pair<const char*, const char*> SHORTCUTS[] = {
            {"Left click", "place a note, drag up or down for a hold"},
            {"Drag a note", "move it (or the whole selection)"},
            {"Shift + drag", "select notes in a box (Ctrl too: add to it)"},
            {"Ctrl + click", "add or remove a note from the selection"},
            {"Ctrl + A", "select all"},
            {"Ctrl + C / X / V", "copy / cut / paste at the playhead"},
            {"Ctrl + Up / Down", "move the selection by one snap"},
            {"Ctrl + Left / Right", "move the selection by one lane"},
            {"M", "mirror the selection (swap left and right)"},
            {"Ctrl + T", "transform the selection (stretch, shrink, mirror)"},
            {"N", "snap notes on or off"},
            {"Right click", "delete a note"},
            {"Wheel", "move by one snap"},
            {"Alt + wheel", "move by one millisecond"},
            {"Alt + arrows", "move by one millisecond"},
            {"Shift + wheel", "change snap"},
            {"Shift + arrows", "change snap"},
            {"Ctrl + wheel", "change scroll speed"},
            {"Ctrl + = / -", "change scroll speed"},
            {"Space", "play or pause"},
            {"Left / Right", "move by one snap"},
            {"Up / Down", "move by one beat"},
            {"Home", "go to the start"},
            {"End", "go to the last note"},
            {"1 - 0", "toggle a note in that lane at the playhead"},
            {"Delete", "delete the selected notes"},
            {"Ctrl + Z / Y", "undo / redo"},
            {"T", "tap tempo (Timing panel)"},
            {"Ctrl + S", "save"},
        };

        if (ImGui::BeginTable("##help", 2, ImGuiTableFlags_SizingStretchProp)) {
            for (const auto& [keys, action] : SHORTCUTS) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(keys);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", action);
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

                if (ImGui::Button("New chart from audio...", ImVec2(260, 0))) {
                    request(Pending::NEW_FROM_AUDIO);
                }

                if (ImGui::Button("Open chart...", ImVec2(260, 0))) {
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
                            perform(pending);
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

        ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Choose a chart", nullptr)) {
            ImGui::TextUnformatted(std::format("{} has {} charts:", path_utf8(choice_source.filename()), chart_choices.size()).c_str());
            ImGui::Spacing();
            std::optional<size_t> picked;

            if (ImGui::BeginChild("##choices", ImVec2(0, std::min(360.0f, ImGui::GetFrameHeightWithSpacing() * (float)chart_choices.size() + 8.0f)), ImGuiChildFlags_Borders)) {
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

            if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
            ImGui::SetNextItemWidth(260);
            ImGui::SliderFloat("Overall difficulty (OD)", &osu_difficulty.overall_difficulty, 0.0f, 10.0f, "%.1f");
            ImGui::SetNextItemWidth(260);
            ImGui::SliderFloat("HP drain", &osu_difficulty.hp_drain, 0.0f, 10.0f, "%.1f");
            osu_difficulty.overall_difficulty = std::round(osu_difficulty.overall_difficulty * 10.0f) / 10.0f;
            osu_difficulty.hp_drain = std::round(osu_difficulty.hp_drain * 10.0f) / 10.0f;
            ImGui::Spacing();

            if (ImGui::Button("Continue", ImVec2(110, 0))) {
                ImGui::CloseCurrentPopup();

                if (document != nullptr) {
                    bool archive = osu_prompt_purpose == DialogPurpose::EXPORT_OSZ;
                    open_export(osu_prompt_purpose, archive ? "Export .osz" : "Export .osu", archive ? ".osz" : ".osu");
                }
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        draw_transform_popup();

        if (audio_prompt_requested) {
            ImGui::OpenPopup("Dropped audio");
            audio_prompt_requested = false;
        }

        if (ImGui::BeginPopupModal("Dropped audio", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(std::format("What should {} be used for?", path_utf8(dropped_audio.filename())).c_str());
            ImGui::Spacing();

            if (ImGui::Button("Change this chart's music", ImVec2(220, 0))) {
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

            if (ImGui::Button("Start a new chart", ImVec2(160, 0))) {
                ImGui::CloseCurrentPopup();
                pending_file = dropped_audio;
                request(Pending::NEW_FROM_AUDIO);
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (confirm_requested) {
            ImGui::OpenPopup("Unsaved changes");
            confirm_requested = false;
        }

        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("This chart has changes that aren't saved.");
            ImGui::Spacing();

            if (ImGui::Button("Save", ImVec2(110, 0))) {
                ImGui::CloseCurrentPopup();

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

            ImGui::SameLine();

            if (ImGui::Button("Discard", ImVec2(110, 0))) {
                ImGui::CloseCurrentPopup();
                perform(pending);
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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

        if (io.WantTextInput || dialog.is_open() || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
            return;
        }

        Input& keys = input();
        bool ctrl = keys.key_held(olc::Key::CTRL);
        bool shift = keys.key_held(olc::Key::SHIFT);

        if (ctrl && keys.key_pressed(olc::Key::N)) {
            request(Pending::NEW_FROM_AUDIO);
        }

        if (ctrl && keys.key_pressed(olc::Key::O)) {
            request(Pending::OPEN_CHART);
        }

        if (ctrl && keys.key_pressed(olc::Key::Q)) {
            request(Pending::QUIT);
        }

        if (document == nullptr) {
            return;
        }

        if (ctrl && keys.key_pressed(olc::Key::S)) {
            if (shift) {
                save_as();
            }
            else {
                save();
            }
        }

        if (ctrl && keys.key_pressed(olc::Key::Z)) {
            if (shift) {
                document->redo();
            }
            else {
                document->undo();
            }
        }

        if (ctrl && keys.key_pressed(olc::Key::Y)) {
            document->redo();
        }

        if (ctrl && keys.key_pressed(olc::Key::T)) {
            if (selection.empty()) {
                set_status("Select some notes first (Shift + drag, or Ctrl + A)", true);
            }
            else {
                transform_requested = true;
            }
        }

        if (ctrl && keys.key_pressed(olc::Key::A)) {
            select_all();
        }

        if (ctrl && keys.key_pressed(olc::Key::C)) {
            copy_selection();
        }

        if (ctrl && keys.key_pressed(olc::Key::X)) {
            cut_selection();
        }

        if (ctrl && keys.key_pressed(olc::Key::V)) {
            paste_clipboard();
        }

        if (ctrl && keys.key_pressed(olc::Key::UP)) {
            nudge_selection(1, 0);
        }

        if (ctrl && keys.key_pressed(olc::Key::DOWN)) {
            nudge_selection(-1, 0);
        }

        if (ctrl && keys.key_pressed(olc::Key::LEFT)) {
            nudge_selection(0, -1);
        }

        if (ctrl && keys.key_pressed(olc::Key::RIGHT)) {
            nudge_selection(0, 1);
        }

        if (ctrl && (keys.key_pressed(olc::Key::EQUALS) || keys.key_pressed(olc::Key::NP_ADD))) {
            change_scroll_speed(1);
        }

        if (ctrl && (keys.key_pressed(olc::Key::MINUS) || keys.key_pressed(olc::Key::NP_SUB))) {
            change_scroll_speed(-1);
        }

        if (ctrl) {
            return;
        }

        if (keys.key_pressed(olc::Key::SPACE) && playback.loaded()) {
            toggle_playback();
        }

        bool later = keys.key_pressed(olc::Key::UP) || keys.key_pressed(olc::Key::RIGHT);
        bool earlier = keys.key_pressed(olc::Key::DOWN) || keys.key_pressed(olc::Key::LEFT);

        if (alt_held()) {
            if (later) {
                seek(position + 1);
            }

            if (earlier) {
                seek(position - 1);
            }
        }
        else if (shift && gesture.kind == Gesture::Kind::NONE) {
            if (later) {
                change_snap(1);
            }

            if (earlier) {
                change_snap(-1);
            }
        }
        else {
            if (keys.key_pressed(olc::Key::LEFT)) {
                step(-1);
            }

            if (keys.key_pressed(olc::Key::RIGHT)) {
                step(1);
            }

            if (keys.key_pressed(olc::Key::DOWN)) {
                step(-(int32_t)divisor);
            }

            if (keys.key_pressed(olc::Key::UP)) {
                step((int32_t)divisor);
            }
        }

        if (keys.key_pressed(olc::Key::HOME)) {
            seek(0);
        }

        if (keys.key_pressed(olc::Key::END) && !document->data().hits.empty()) {
            seek(document->data().hits.back().time);
        }

        if (keys.key_pressed(olc::Key::T)) {
            tap_tempo();
        }

        if (keys.key_pressed(olc::Key::M)) {
            mirror_selection();
        }

        if (keys.key_pressed(olc::Key::N)) {
            snap_enabled = !snap_enabled;
            set_status(snap_enabled ? "Snapping notes to the grid" : "Not snapping, notes go to the exact millisecond");
        }

        if (keys.key_pressed(olc::Key::ESCAPE)) {
            selection.clear();
        }

        if (keys.key_pressed(olc::Key::DEL) || keys.key_pressed(olc::Key::BACK)) {
            delete_selection();
        }

        constexpr olc::Key LANE_KEYS[] = {olc::Key::K1, olc::Key::K2, olc::Key::K3, olc::Key::K4, olc::Key::K5, olc::Key::K6, olc::Key::K7, olc::Key::K8, olc::Key::K9, olc::Key::K0};
        uint8_t key_count = document->data().info.key_count;

        for (uint8_t lane = 0; lane < key_count && lane < std::size(LANE_KEYS); lane++) {
            if (!keys.key_pressed(LANE_KEYS[lane])) {
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
        double lane = std::min(screen.y * skin_setting(*skin, skin_settings::LANE_WIDTH) / 100.0, (double)view.w / keys);
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

                    if (ImGui::Button(std::format("{}##{}", factor, title).c_str(), ImVec2(70, 0))) {
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

            if (ImGui::Button("Mirror (M)", ImVec2(110, 0))) {
                mirror_selection();
            }

            int key_count = std::max<int>(document->data().info.key_count, 2);
            ImGui::SetNextItemWidth(110);
            ImGui::InputInt("Columns", &rotate_columns);
            rotate_columns = std::clamp(rotate_columns, 1, key_count - 1);
            ImGui::SameLine();

            if (ImGui::Button("Rotate left", ImVec2(110, 0))) {
                rotate_selection(-rotate_columns);
            }

            ImGui::SameLine();

            if (ImGui::Button("Rotate right", ImVec2(110, 0))) {
                rotate_selection(rotate_columns);
            }
        }

        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(110, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
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
                    labels->AddText(ImVec2((float)field->pos.x - size.x - 8.0f, y - size.y / 2), IM_COL32(255, 255, 255, 170), label.c_str());
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
            ImVec2 corner(right + 10.0f, y - size.y / 2);
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

        float height = view.h - DENSITY_BAR_MARGIN * 2;
        ImVec2 corner(view.x + view.w - DENSITY_BAR_WIDTH - DENSITY_BAR_MARGIN, view.y + DENSITY_BAR_MARGIN);
        size_t rows = (size_t)std::max(1.0f, height / DENSITY_ROW_PX);
        double length = timeline_length();
        update_density(rows, length);

        ImGui::SetNextWindowPos(corner);
        ImGui::SetNextWindowSize(ImVec2(DENSITY_BAR_WIDTH, height));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::Begin("##density_bar", nullptr, flags)) {
            ImGui::InvisibleButton("##density_seek", ImVec2(DENSITY_BAR_WIDTH, height));
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

            draw->AddRectFilled(corner, ImVec2(corner.x + DENSITY_BAR_WIDTH, bottom), IM_COL32(12, 12, 18, 200), 4.0f);

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
                float width = (DENSITY_BAR_WIDTH - 6.0f) * t;
                float y = bottom - (float)(row + 1) * DENSITY_ROW_PX;
                draw->AddRectFilled(ImVec2(corner.x + 3.0f, y), ImVec2(corner.x + 3.0f + width, y + DENSITY_ROW_PX), ImGui::ColorConvertFloat4ToU32(color));
            }

            for (const ChartTimingPoint& point : document->data().timing_points) {
                float y = y_of(point.time);
                draw->AddLine(ImVec2(corner.x + DENSITY_BAR_WIDTH - 8.0f, y), ImVec2(corner.x + DENSITY_BAR_WIDTH, y), IM_COL32(255, 140, 60, 230), 2.0f);
            }

            if (field != nullptr) {
                float top = y_of(field->time_at(view.y, window));
                float low = y_of(std::max(0.0, field->time_at(view.y + view.h, window)));
                draw->AddRectFilled(ImVec2(corner.x, top), ImVec2(corner.x + DENSITY_BAR_WIDTH, low), IM_COL32(255, 255, 255, 28));
                draw->AddRect(ImVec2(corner.x, top), ImVec2(corner.x + DENSITY_BAR_WIDTH, low), IM_COL32(255, 255, 255, 70));
            }

            float playhead = y_of(position);
            draw->AddLine(ImVec2(corner.x - 3.0f, playhead), ImVec2(corner.x + DENSITY_BAR_WIDTH + 3.0f, playhead), IM_COL32(255, 255, 255, 255), 2.0f);
            draw->AddRect(corner, ImVec2(corner.x + DENSITY_BAR_WIDTH, bottom), IM_COL32(255, 255, 255, hovered || active ? 120 : 50), 4.0f);

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
            std::array<float, WaveformBands::BAND_COUNT> levels = waveform.peak(older + WAVEFORM_VISUAL_OFFSET_MS, newer + WAVEFORM_VISUAL_OFFSET_MS);

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
