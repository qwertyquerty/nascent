#include "Game.h"
#include "TextInput.h"
#include "WindowHook.h"

#include <algorithm>
#include <cmath>

namespace nascent {
    Game::Game(olc::PixelGameEngine* window) {
        this->window = window;
    };

    Game::~Game() {
        if (key_event_hook_active) {
            uninstall_window_hook();
        }
    }

    void Game::init_input() {
        key_event_hook_active = install_window_hook(&key_event_queue, window->sAppName);
    }

    void Game::make_layer_smooth(uint32_t layer) {
        olc::vi2d size = window->GetScreenSize();
        window->GetLayers()[layer].pDrawTarget.Create(size.x, size.y, true);
    }

    void Game::use_smooth_scaling() {
        make_layer_smooth(0);
        window->SetDrawTarget(nullptr);
    }

    void Game::poll_key_events() {
        double now = now_ms();

        for (size_t index = 1; index < KEY_COUNT; index++) {
            olc::Key key = (olc::Key)index;
            key_event_queue.push(key, window->GetKey(key).bHeld, now);
        }
    }

    void Game::begin_frame(float elapsed_time) {
        if (!key_event_hook_active) {
            poll_key_events();
        }

        frame_key_events = key_event_queue.drain();
        frame_text = key_event_hook_active ? key_event_queue.drain_text() : us_layout_text(window);
        update_transition(elapsed_time);
        apply_pending_scene();
    }

    void Game::update_transition(float elapsed_time) {
        if (outgoing_scene == nullptr) {
            return;
        }

        transition_time += elapsed_time;

        if (transition_time >= scene_transition_time) {
            outgoing_scene = nullptr;
            outgoing_owned_scene.reset();
        }
    }

    void Game::draw_scenes(olc::PixelGameEngine* window) {
        if (outgoing_scene != nullptr) {
            if (transition_layer < 0) {
                transition_layer = (int32_t)window->CreateLayer();
                make_layer_smooth((uint32_t)transition_layer);
            }

            window->EnableLayer((uint8_t)transition_layer, true);
            window->SetDrawTarget((uint8_t)transition_layer);
            window->Clear(olc::BLACK);
            outgoing_scene->draw(window);
        }
        else if (transition_layer >= 0) {
            window->EnableLayer((uint8_t)transition_layer, false);
        }

        window->SetDrawTarget((uint8_t)0);
        window->Clear(olc::BLACK);

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

        audio.Play(id, looping);
    }

    void Game::stop_sound(int id) {
        if (id != -1) {
            audio.Stop(id);
        }
    }

    const std::string& Game::get_frame_text() {
        return frame_text;
    }

    const std::vector<KeyEvent>& Game::get_frame_key_events() {
        return frame_key_events;
    }

    void Game::quit() {
        quit_requested = true;
    }

    bool Game::should_quit() {
        return quit_requested;
    }
}
