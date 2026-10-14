#include "Game.h"
#include "KeySource.h"
#include "Platform.h"
#include "TextInput.h"
#include "WindowHook.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <utility>

namespace nascent {
    Game::Game(olc::PixelGameEngine* window) {
        this->window = window;
    };

    Game::~Game() {
        stop_key_source();

        if (window_hook_active) {
            uninstall_window_hook();
        }
    }

    void Game::init_input() {
        window_hook_active = install_window_hook(&key_event_queue, window->sAppName);
        input_mode = window_hook_active ? InputMode::WINDOW_MESSAGES : InputMode::POLLING;

        if (std::optional<InputMode> source = start_key_source(&key_event_queue)) {
            input_mode = *source;
        }

        set_window_hook_key_events(input_mode == InputMode::WINDOW_MESSAGES);
    }

    InputMode Game::get_input_mode() const {
        return input_mode;
    }

    void Game::make_layer_blank(uint32_t layer) {
        window->GetLayers()[layer].pDrawTarget.Create(1, 1);
    }

    void Game::use_blank_canvas() {
        make_layer_blank(0);
        window->SetDrawTarget(nullptr);
    }

    void Game::track_screen_size() {
        olc::vi2d size = window->GetScreenSize();

        if (size == screen_size) {
            return;
        }

        screen_size = size;
        use_blank_canvas();

        if (transition_layer >= 0) {
            make_layer_blank((uint32_t)transition_layer);
        }

        on_screen_resize();

        std::array<Scene*, 3> resized = {scene, outgoing_scene, pending_scene};

        for (size_t index = 0; index < resized.size(); index++) {
            bool repeated = std::find(resized.begin(), resized.begin() + index, resized[index]) != resized.begin() + index;

            if (resized[index] != nullptr && resized[index]->initialized && !repeated) {
                resized[index]->on_resize();
            }
        }
    }

    void Game::poll_key_events() {
        double now = now_ms();

        for (size_t index = 1; index < KEY_COUNT; index++) {
            olc::Key key = (olc::Key)index;
            key_event_queue.push(key, window->GetKey(key).bHeld, now);
        }
    }

    void Game::begin_frame(float elapsed_time) {
        if (input_mode == InputMode::POLLING) {
            poll_key_events();
        }

        frame_key_events = key_event_queue.drain();
        frame_text = window_hook_active ? key_event_queue.drain_text() : us_layout_text(window);
        frame_dropped_files = key_event_queue.drain_dropped_files();
        track_screen_size();
        update_transition(elapsed_time);
        apply_pending_scene();
    }

    void Game::update_transition(float elapsed_time) {
        if (outgoing_scene == nullptr) {
            return;
        }

        if (std::exchange(skip_transition_step, false)) {
            return;
        }

        transition_time += std::min(elapsed_time, MAX_TRANSITION_STEP);

        if (transition_time >= scene_transition_time) {
            outgoing_scene = nullptr;
            outgoing_owned_scene.reset();
        }
    }

    void Game::draw_scenes(olc::PixelGameEngine* window) {
        if (outgoing_scene != nullptr) {
            if (transition_layer < 0) {
                transition_layer = (int32_t)window->CreateLayer();
                make_layer_blank((uint32_t)transition_layer);
            }

            window->EnableLayer((uint8_t)transition_layer, true);
            window->SetDrawTarget((uint8_t)transition_layer);
            window->Clear(clear_color);
            outgoing_scene->draw(window);
        }
        else if (transition_layer >= 0) {
            window->EnableLayer((uint8_t)transition_layer, false);
        }

        window->SetDrawTarget((uint8_t)0);
        window->Clear(clear_color);

        if (scene != nullptr) {
            scene->draw(window);
        }

        float progress = 1.0f;

        if (outgoing_scene != nullptr && scene_transition_time > 0) {
            float t = std::clamp(transition_time / scene_transition_time, 0.0f, 1.0f);
            progress = t * t * (3 - 2 * t);
        }

        uint8_t alpha = (uint8_t)std::lround(progress * 255);
        window->SetLayerTint(0, olc::Pixel(255, 255, 255, alpha));

        if (alpha < 255) {
            for (olc::DecalInstance& decal : window->GetLayers()[0].vecDecalInstance) {
                for (olc::Pixel& tint : decal.tint) {
                    tint.a = (uint8_t)(tint.a * alpha / 255);
                }
            }
        }
    }

    void Game::set_scene(Scene* scene) {
        pending_scene = scene;
        pending_owned_scene.reset();
    }

    void Game::set_scene(std::unique_ptr<Scene> scene) {
        pending_scene = scene.get();
        pending_owned_scene = std::move(scene);
    }

    void Game::limit_frame_rate(uint16_t limit) {
        if (limit == 0) {
            next_frame.reset();
            return;
        }

        using clock = std::chrono::steady_clock;
        clock::duration period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / limit));
        clock::time_point now = clock::now();

        if (!next_frame || now - *next_frame > period) {
            next_frame = now + period;
            return;
        }

        std::chrono::duration<double> remaining = *next_frame - now;

        if (remaining > FRAME_SPIN_TIME) {
            precise_sleep((remaining - FRAME_SPIN_TIME).count());
        }

        while (clock::now() < *next_frame) {
            std::this_thread::yield();
        }

        *next_frame += period;
    }

    bool Game::is_transitioning() const {
        return outgoing_scene != nullptr;
    }

    void Game::apply_pending_scene() {
        if (pending_scene == nullptr) {
            return;
        }

        if (pending_scene == scene) {
            pending_scene = nullptr;
            return;
        }

        if (scene != nullptr) {
            scene->on_exit();
            outgoing_owned_scene = std::move(owned_scene);
            outgoing_scene = scene;
            transition_time = 0;
            skip_transition_step = true;
        }

        scene = pending_scene;
        owned_scene = std::move(pending_owned_scene);
        pending_scene = nullptr;

        scene->set_game(this);

        if (!scene->initialized) {
            scene->init(this);
            scene->initialized = true;
        }

        scene->on_enter();
    }

    void Game::release_scenes() {
        if (scene != nullptr) {
            scene->on_exit();
        }

        scene = nullptr;
        pending_scene = nullptr;
        outgoing_scene = nullptr;
        owned_scene.reset();
        pending_owned_scene.reset();
        outgoing_owned_scene.reset();
    }

    olc::MiniAudio& Game::get_audio() {
        return this->audio;
    }

    void Game::play_sound(int id, bool looping) {
        if (id == -1) {
            return;
        }

        if (audio.IsPlaying(id)) {
            audio.Stop(id);
        }

        audio.SetVolume(id, effects_volume);
        audio.Play(id, looping);
    }

    void Game::play_music(int id, bool looping) {
        if (id == -1) {
            return;
        }

        if (audio.IsPlaying(id)) {
            audio.Stop(id);
        }

        audio.SetVolume(id, music_volume);
        audio.Play(id, looping);
    }

    void Game::apply_music_volume(int id, float fade) {
        audio.SetVolume(id, music_volume * fade);
    }

    void Game::apply_effects_volume(int id) {
        audio.SetVolume(id, effects_volume);
    }

    void Game::set_volumes(float master, float music, float effects) {
        music_volume = std::clamp(music, 0.0f, 1.0f);
        effects_volume = std::clamp(effects, 0.0f, 1.0f);
        ma_engine_set_volume(audio.GetEngine(), std::clamp(master, 0.0f, 1.0f));
    }

    void Game::stop_sound(int id) {
        if (id != -1) {
            audio.Stop(id);
        }
    }

    const std::string& Game::get_frame_text() {
        return frame_text;
    }

    InputDelayStats Game::input_delay() const {
        return key_event_queue.input_delay();
    }

    void Game::reset_input_delay() {
        key_event_queue.reset_input_delay();
    }

    const std::vector<std::string>& Game::get_frame_dropped_files() const {
        return frame_dropped_files;
    }

    const std::vector<KeyEvent>& Game::get_frame_key_events() {
        return frame_key_events;
    }

    void Game::notify(const std::string&, bool) {
    }

    void Game::quit() {
        quit_requested = true;
    }

    bool Game::should_quit() {
        return quit_requested;
    }
}
