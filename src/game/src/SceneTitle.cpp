#include "SceneTitle.h"
#include "RichPresence.h"
#include "Account.h"
#include "Attempt.h"
#include "Chart.h"
#include "EntityFFT.h"
#include "EntityField.h"
#include "EntityStarfield.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "gse/input/KeyEvents.h"
#include "SceneAccount.h"
#include "QuitOverlay.h"
#include "SceneList.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/ProgressBar.h"
#include "gse/ui/Ui.h"
#include "Updater.h"
#include "Utf8Path.h"
#include "gse/render/SpriteDecal.h"
#include "gse/render/Color.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <utility>

namespace nascent {
    SceneTitle::SceneTitle(Skin* bg_skin, Skin* logo_skin, SceneList* list, EntityStarfield* sky, std::optional<TitleTrack> handoff) {
        this->bg_skin = bg_skin;
        this->logo_skin = logo_skin;
        this->list = list;
        this->sky = sky;
        this->handoff = std::move(handoff);
    };

    SceneTitle::~SceneTitle() {
    }

    std::unique_ptr<Chart> SceneTitle::load_random_chart(std::vector<std::filesystem::path> candidates) {
        std::error_code ec;

        for (int32_t attempt = 0; attempt < RANDOM_CHART_ATTEMPTS && !candidates.empty(); attempt++) {
            size_t index = (size_t)std::rand() % candidates.size();
            std::string error;
            std::unique_ptr<Chart> chart = load_chart_file(candidates[index], 1.0, error);

            if (chart != nullptr && std::filesystem::exists(chart->audio_path, ec)) {
                return chart;
            }

            std::cerr << "Skipping title chart " << path_utf8(candidates[index]) << ": " << (chart == nullptr ? error : "its audio is missing") << std::endl;
            candidates.erase(candidates.begin() + (std::ptrdiff_t)index);
        }

        return nullptr;
    }

    bool SceneTitle::load_background() {
        if (SettingsManager::settings.title_songs == TitleSongs::NONE) {
            return false;
        }

        if (!list->library_available()) {
            waiting_for_library = true;
            return false;
        }

        std::unique_ptr<Chart> chart;

        if (SettingsManager::settings.title_songs == TitleSongs::FAVORITES) {
            chart = load_random_chart(list->chart_paths(true).value_or(std::vector<std::filesystem::path>()));
        }

        if (chart == nullptr) {
            chart = load_random_chart(list->chart_paths(false).value_or(std::vector<std::filesystem::path>()));
        }

        if (chart == nullptr) {
            std::cerr << "No chart in " << SettingsManager::settings.list_song_directory << " could be loaded for the title screen background" << std::endl;
            return false;
        }

        if (bg_audio_id != -1) {
            game->audio().stop(bg_audio_id);
            game->audio().engine().UnloadSound(bg_audio_id);
        }

        build_background(std::move(chart));
        bg_audio_id = load_song_audio(game->audio().engine(), bg_chart->audio_path);
        game->audio().apply_music_volume(bg_audio_id);
        bg_clock.reset(0, now_ms());
        return true;
    }

    bool SceneTitle::adopt_track(const TitleTrack& track) {
        std::string error;
        std::unique_ptr<Chart> chart = SettingsManager::settings.title_songs == TitleSongs::NONE ? nullptr : load_chart_file(track.chart_path, 1.0, error);

        if (chart == nullptr || track.audio_id == -1) {
            if (track.audio_id != -1) {
                game->audio().stop(track.audio_id);
                game->audio().engine().UnloadSound(track.audio_id);
            }

            return false;
        }

        build_background(std::move(chart));
        bg_audio_id = track.audio_id;
        ma_sound_set_looping(game->audio().engine().GetSound(bg_audio_id), MA_FALSE);
        bg_clock.reset(game->audio().engine().GetPositionMilliseconds(bg_audio_id), now_ms());
        bg_started = true;
        bg_fade = 0;
        return true;
    }

    void SceneTitle::build_background(std::unique_ptr<Chart> chart) {
        bg_chart = std::move(chart);
        logo_note_cursor = 0;
        logo_light_position = 0;
        letter_hit_time.clear();
        letter_hold_end.clear();
    }

    static size_t chart_letter(uint8_t key, uint8_t key_count, size_t letters) {
        if (key_count == 0 || letters == 0) {
            return 0;
        }

        return std::min(letters - 1, (size_t)((key + 0.5) * (double)letters / key_count));
    }

    void SceneTitle::update_logo_lights() {
        size_t letters = logo_field->get_key_count();

        if (letter_hit_time.size() != letters) {
            letter_hit_time.assign(letters, -std::numeric_limits<double>::infinity());
            letter_hold_end.assign(letters, -std::numeric_limits<double>::infinity());
            logo_note_cursor = 0;
        }

        if (bg_chart != nullptr && bg_started) {
            if (bg_position < logo_light_position) {
                std::fill(letter_hit_time.begin(), letter_hit_time.end(), -std::numeric_limits<double>::infinity());
                std::fill(letter_hold_end.begin(), letter_hold_end.end(), -std::numeric_limits<double>::infinity());
                logo_note_cursor = 0;
            }

            logo_light_position = bg_position;
            const std::vector<Hit>& hits = bg_chart->hits;

            while (logo_note_cursor < hits.size() && hits[logo_note_cursor].time <= bg_position) {
                const Hit& hit = hits[logo_note_cursor++];
                size_t letter = chart_letter(hit.key, bg_chart->info.key_count, letters);
                letter_hit_time[letter] = hit.time;

                if (hit.hit_type == HitType::HOLD) {
                    letter_hold_end[letter] = std::max(letter_hold_end[letter], (double)hit.end_time);
                }
            }
        }

        logo_field->lane_light.resize(letters);
        double light_fade_ms = Ui::layout("title.logo_light_fade_ms");

        for (size_t letter = 0; letter < letters; letter++) {
            double since = bg_position - std::max(letter_hit_time[letter], letter_hold_end[letter]);
            logo_field->lane_light[letter] = bg_position < letter_hold_end[letter] ? 1.0f : (float)std::clamp(1.0 - since / light_fade_ms, 0.0, 1.0);
        }
    }

    void SceneTitle::on_resize() {
        if (!loaded) {
            return;
        }

        olc::vi2d window_size = game->window->GetScreenSize();
        double content = std::min((double)window_size.x, window_size.y * Ui::REFERENCE_WIDTH / Ui::REFERENCE_HEIGHT);

        double logo_width = content * Ui::layout("title.logo_width_fraction");
        double visualizer_height = (double)window_size.y * Ui::layout("title.visualizer_height_fraction");

        logo_field->pos = {((double)window_size.x - logo_width) / 2, 0};
        logo_field->size = {logo_width, (double)window_size.y * Ui::layout("title.logo_height_fraction") + content * Ui::layout("title.logo_extra_height_fraction")};
        logo_field->set_lane_gap(std::nullopt);
        fft_l->pos = {0, 0};
        fft_l->size = {(double)window_size.x, visualizer_height};
        fft_r->pos = {0, (double)window_size.y - visualizer_height};
        fft_r->size = {(double)window_size.x, visualizer_height};

        logo_skin->set_font_scale(Ui::scale());
        start_hint = render_text_decal(bg_skin->display_font_medium, START_HINT, olc::WHITE);
    }

    void SceneTitle::on_create() {
        RichPresence::in_menu("On the title screen");
        std::string error;
        sky->show(SkyView::TITLE);

        logo_chart = Chart::load(LOGO_CHART_PATH, 1.0, error);
        if (logo_chart == nullptr) {
            std::cerr << "Failed to load title logo chart: " << error << std::endl;
            return;
        }

        if (!handoff || !adopt_track(*std::exchange(handoff, std::nullopt))) {
            load_background();
        }

        logo_attempt = std::make_unique<Attempt>(logo_chart.get());

        logo_field = std::make_unique<EntityField>(logo_chart.get(), logo_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1});
        logo_field->set_attempt(logo_attempt.get());
        logo_field->draw_notes_past_judge = false;
        logo_field->draw_judge_colors = true;
        logo_field->draw_hit_score = false;
        logo_field->judge_alpha = (uint8_t)Ui::layout("title.logo_judge_alpha");
        logo_field->scroll_speed = SettingsManager::settings.field_scroll_speed;
        logo_field->opacity = PLAY_LOGO_INTRO ? 1.0f : 0.0f;

        fft_l = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1}, 0);
        fft_l->opacity = (float)Ui::layout("title.visualizer_opacity");
        fft_l->init();

        fft_r = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1}, 1);
        fft_r->flipped = true;
        fft_r->mirrored = true;
        fft_r->opacity = (float)Ui::layout("title.visualizer_opacity");
        fft_r->init();

        if constexpr (PLAY_LOGO_INTRO) {
            logo_audio_id = load_song_audio(game->audio().engine(), logo_chart->audio_path);
        }
        else {
            logo_started = true;
            logo_position = logo_chart->get_chart_end_ms() + LOGO_FINISHED_OFFSET_MS;
        }

        logo_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;
        bg_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;

        loading = !list->library_available();
        reveal = loading ? 0.0f : reveal_time();
        loaded = true;
        on_resize();
    };

    bool SceneTitle::logo_playing() {
        return logo_audio_id != -1 && game->audio().engine().IsPlaying(logo_audio_id);
    }

    bool SceneTitle::logo_finished() {
        if constexpr (PLAY_LOGO_INTRO) {
            return logo_started && !logo_playing();
        }

        return timer >= LOGO_FADE_DELAY + LOGO_FADE_IN_TIME + LOGO_HOLD_TIME;
    }

    void SceneTitle::on_exit() {
        for (int* id : {&logo_audio_id, &bg_audio_id}) {
            if (*id != -1) {
                game->audio().engine().Stop(*id);
                game->audio().engine().UnloadSound(*id);
                *id = -1;
            }
        }

        loaded = false;
    }

    void SceneTitle::update(float elapsed_time) {
        if (Updater::ready()) {
            game->quit();
            return;
        }

        if (InputManager::update_pressed(game->input())) {
            Updater::start();
        }

        if (spots.take_click(game->input()) == SPOT_UPDATE) {
            Updater::start();
        }
        else if (game->input().take_click(0)) {
            game->input().inject(actions::CONFIRM);
        }

        if (game->input().take(actions::BACK)) {
            game->audio().play_effect(bg_skin->sound(SkinSound::HITSOUND));
            overlays.open(std::make_unique<QuitOverlay>(bg_skin));
            return;
        }

        if (loaded && bar_opacity() > 0.0f) {
            update_loading(elapsed_time);
            return;
        }

        if (game->input().take(actions::CONFIRM)) {
            bool sign_in_first = !Account::signed_in();

            if (bg_chart != nullptr) {
                bool playing = !sign_in_first && bg_started && bg_audio_id != -1 && game->audio().engine().IsPlaying(bg_audio_id);
                list->select_chart_by_hash(bg_chart->gameplay_hash(), bg_chart->file_path, playing ? bg_audio_id : -1);

                if (playing) {
                    bg_audio_id = -1;
                }
            }

            game->audio().play_effect(bg_skin->get_random_menu_pluck());

            if (sign_in_first) {
                game->scenes().replace(std::make_unique<SceneAccount>(bg_skin, list, sky, true));
            }
            else {
                game->scenes().replace(list);
            }

            return;
        }

        if (!loaded) {
            return;
        }

        double now = now_ms();
        float step = std::min(elapsed_time, MAX_TIMER_STEP);
        sky->update(step);

        if (timer > SettingsManager::settings.title_logo_start_delay && !logo_started) {
            logo_started = true;
            game->audio().engine().Play(logo_audio_id);
        }

        if (waiting_for_library && list->library_available()) {
            waiting_for_library = false;
            load_background();
        }

        if (bg_chart != nullptr && logo_finished() && !game->audio().engine().IsPlaying(bg_audio_id)) {
            if (bg_started) {
                load_background();
            }

            bg_started = true;
            bg_fade = 0;
            game->audio().engine().Play(bg_audio_id);
        }

        if (bg_started) {
            bg_fade = std::min(1.0f, bg_fade + step / (float)Ui::layout("title.start_hint_fade_in_time"));
        }

        if (logo_audio_id != -1) {
            logo_clock.sync(game->audio().engine().GetPositionMilliseconds(logo_audio_id), now);
            logo_position = logo_clock.position_at(now);
        }

        if constexpr (!PLAY_LOGO_INTRO) {
            logo_field->opacity = std::clamp(((float)timer - LOGO_FADE_DELAY) / LOGO_FADE_IN_TIME, 0.0f, 1.0f);
        }

        uint32_t logo_keys = logo_chart->held_lanes_at(logo_position);
        logo_attempt->set_keys(logo_keys, (int32_t)logo_position);
        logo_field->set_keys(logo_keys);
        logo_field->set_position(logo_position);

        if (bg_chart != nullptr) {
            bg_clock.sync(game->audio().engine().GetPositionMilliseconds(bg_audio_id), now);
            bg_position = bg_clock.position_at(now);

            if (game->audio().engine().IsPlaying(bg_audio_id)) {
                game->audio().engine().CopyEngineBuffer(audio_buffer);
                fft_l->update_song_buffer(audio_buffer, elapsed_time);
                fft_r->update_song_buffer(audio_buffer, elapsed_time);
            }
        }

        update_logo_lights();
        reveal = std::min(reveal + step, reveal_time());
        timer += step;
    };

    void SceneTitle::draw_update_notice(olc::PixelGameEngine* window) {
        UpdateSnapshot update = Updater::snapshot();

        if (!update.release) {
            return;
        }

        std::string text;
        olc::Pixel color = bg_skin->color(SkinColor::UPDATE_NOTICE);

        switch (update.status) {
            case UpdateStatus::AVAILABLE:
                text = std::format("Nascent {} is available, click here or press Ctrl+R to {}", update.release->tag, update.installable ? "update" : "open the download page");
                break;
            case UpdateStatus::DOWNLOADING:
                text = std::format("Downloading Nascent {}... {}%", update.release->tag, (int)std::floor(update.progress * 100.0));
                break;
            case UpdateStatus::READY:
                text = "Restarting to finish the update...";
                break;
            case UpdateStatus::FAILED:
                text = std::format("Update failed: {}. Click here or press Ctrl+R to try again", update.error);
                color = bg_skin->color(SkinColor::UPDATE_NOTICE_ERROR);
                break;
            default:
                return;
        }

        olc::vi2d screen = window->GetScreenSize();
        int32_t width = text_bounds(bg_skin->display_font_small, text).size.x;
        bool clickable = update.status == UpdateStatus::AVAILABLE || update.status == UpdateStatus::FAILED;
        draw_hints(window, bg_skin->display_font_small, {{text, clickable ? SPOT_UPDATE : -1}}, {(float)((screen.x - width) / 2), (float)(screen.y - Ui::layout_px("title.update_notice_bottom"))}, 0.0f, color, bg_skin->color(SkinColor::TEXT), spots);
    }

    void SceneTitle::update_loading(float elapsed_time) {
        float step = std::min(elapsed_time, MAX_TIMER_STEP);

        if (!loading) {
            reveal += step;
            return;
        }

        shown_progress += (list->library_progress() - shown_progress) * std::min(1.0f, step * (float)Ui::layout("title.loading_bar_speed"));

        if (!list->library_available()) {
            return;
        }

        if (waiting_for_library) {
            waiting_for_library = false;
            load_background();
        }

        warmup_frames++;

        if (warmup_frames > WARMUP_FRAMES && shown_progress >= 0.99f) {
            loading = false;
        }
    }

    void SceneTitle::draw_loading(olc::PixelGameEngine* window) {
        float logo_opacity = std::exchange(logo_field->opacity, 1.0f);
        logo_field->draw(window);
        logo_field->opacity = logo_opacity;
        fft_l->draw(window);
        fft_r->draw(window);

        draw_cover(window, 1.0f);

        float opacity = bar_opacity();
        olc::vf2d screen = olc::vf2d(window->GetScreenSize());
        olc::vf2d size = {Ui::layout_pxf("title.loading_bar_width"), std::max(1.0f, Ui::layout_pxf("title.loading_bar_height"))};
        ProgressBar bar(size, fade(bg_skin->color(SkinColor::PROGRESS_TRACK), (float)Ui::layout("title.loading_track_opacity") * opacity));
        bar.draw(window, (screen - size) / 2.0f, shown_progress, fade(bg_skin->accent(0.5), (float)Ui::layout("title.loading_fill_opacity") * opacity));
    }

    void SceneTitle::draw_cover(olc::PixelGameEngine* window, float opacity) {
        olc::Pixel cover = bg_skin->color(SkinColor::SCREEN_BACKGROUND);
        cover.a = 255;
        window->FillRectDecal({0.0f, 0.0f}, olc::vf2d(window->GetScreenSize()), fade(cover, opacity));
    }

    float SceneTitle::reveal_time() const {
        return (float)Ui::layout("title.loading_bar_fade_time") + (float)Ui::layout("title.starfield_fade_in_time");
    }

    float SceneTitle::bar_opacity() const {
        return loading ? 1.0f : 1.0f - std::clamp(reveal / (float)Ui::layout("title.loading_bar_fade_time"), 0.0f, 1.0f);
    }

    float SceneTitle::cover_opacity() const {
        return loading ? 1.0f : 1.0f - std::clamp((reveal - (float)Ui::layout("title.loading_bar_fade_time")) / (float)Ui::layout("title.starfield_fade_in_time"), 0.0f, 1.0f);
    }

    void SceneTitle::draw(olc::PixelGameEngine* window) {
        sky->draw(window);
        spots.clear();

        if (!loaded) {
            draw_update_notice(window);
            return;
        }

        if (bar_opacity() > 0.0f) {
            draw_loading(window);
            return;
        }

        if (cover_opacity() > 0.0f) {
            draw_cover(window, cover_opacity());
        }

        if (bg_started && bg_chart != nullptr) {
            fft_l->draw(window);
            fft_r->draw(window);
        }

        logo_field->draw(window);

        if (start_hint && (bg_started || (bg_chart == nullptr && logo_finished()))) {
            olc::Decal* hint = start_hint.get();
            olc::vi2d screen = window->GetScreenSize();
            float pulse = 0.5f + 0.5f * std::sin((float)timer * (float)Ui::layout("title.start_hint_pulse_speed"));
            float visibility = bg_chart != nullptr ? bg_fade : 1.0f;
            float alpha = (float)Ui::layout("title.start_hint_min_alpha") + (float)Ui::layout("title.start_hint_pulse_alpha") * pulse;
            float y = (float)(logo_field->pos.y + logo_field->size.y + Ui::layout_px("title.start_hint_gap"));
            window->DrawDecal({(float)(screen.x - hint->sprite->width) / 2.0f, y}, hint, {1.0f, 1.0f}, fade(bg_skin->color(SkinColor::TEXT), alpha * visibility / 255.0));
        }

        draw_update_notice(window);
    };
}
