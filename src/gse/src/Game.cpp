#include "gse/Game.h"
#include "gse/render/Canvas.h"

#include <utility>

namespace nascent {
    Game::Game(olc::PixelGameEngine* window) : input_state(window), scene_stack(this), window(window) {
        global_overlays.bind(this);
    }

    Game::~Game() = default;

    void Game::use_blank_canvas() {
        window->EnablePixelTransfer(false);
        window->GetLayers()[0].pDrawTarget.Create(1, 1);
        window->SetDrawTarget(nullptr);
    }

    void Game::track_screen_size() {
        olc::vi2d size = window->GetScreenSize();

        if (size == screen_size) {
            return;
        }

        screen_size = size;
        use_blank_canvas();

        if (transition != nullptr) {
            transition->resize(window);
        }

        on_screen_resize();
        scene_stack.for_each_created([](Scene* scene) { scene->on_resize(); });
    }

    void Game::begin_frame(float elapsed_time) {
        input_state.begin_frame();
        track_screen_size();
        mixer.update(elapsed_time);
        scene_stack.advance(elapsed_time);
        scene_stack.apply();
    }

    void Game::update_scene(float elapsed_time) {
        global_overlays.update(elapsed_time);
        Scene* scene = scene_stack.top();

        if (scene == nullptr) {
            return;
        }

        scene->overlays.update(elapsed_time);

        {
            SuppressedInput muted(input_state, scene->overlays.has_modal());
            scene->update(elapsed_time);
        }

        if (scene_stack.depth() > 1 && scene_stack.top() == scene && input_state.take(actions::BACK)) {
            scene_stack.pop();
            on_back();
        }
    }

    void Game::draw_scenes(olc::PixelGameEngine* window) {
        Scene* incoming = scene_stack.top();
        Scene* outgoing = scene_stack.outgoing_scene();

        auto draw_scene = [window](Scene* scene) {
            if (scene != nullptr) {
                scene->draw(window);
                scene->overlays.draw(window);
            }
        };

        if (transition != nullptr && outgoing != nullptr && outgoing != incoming) {
            transition->draw(window, scene_stack.transition_progress(), clear_color, [&] { draw_scene(outgoing); }, [&] { draw_scene(incoming); });
            return;
        }

        if (transition != nullptr) {
            transition->idle(window);
        }

        paint_layer(window, 0, clear_color);
        draw_scene(incoming);
    }

    void Game::release_scenes() {
        scene_stack.clear();
        global_overlays.clear();
    }

    SceneStack& Game::scenes() {
        return scene_stack;
    }

    Input& Game::input() {
        return input_state;
    }

    AudioMixer& Game::audio() {
        return mixer;
    }

    OverlayStack& Game::overlays() {
        return global_overlays;
    }

    void Game::set_transition(std::unique_ptr<Transition> transition) {
        if (this->transition != nullptr) {
            this->transition->idle(window);
        }

        this->transition = std::move(transition);

        if (this->transition != nullptr) {
            this->transition->prepare(window);
        }

        scene_stack.set_transition_duration(this->transition != nullptr ? this->transition->duration() : 0.0f);
    }

    void Game::limit_frame_rate(uint16_t frames_per_second) {
        frame_clock.limit(frames_per_second);
    }

    void Game::notify(const std::string&, bool) {
    }

    void Game::quit() {
        quit_requested = true;
    }

    bool Game::should_quit() const {
        return quit_requested;
    }
}
