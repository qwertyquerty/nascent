#include "gse/SceneStack.h"

#include <algorithm>
#include <utility>

namespace nascent {
    SceneStack::SceneStack(Game* game) : game(game) {}

    SceneStack::~SceneStack() {
        clear();
    }

    void SceneStack::destroy(Entry& entry) {
        if (entry.owned != nullptr) {
            entry.owned->on_destroy();
            entry.owned.reset();
        }

        entry.scene = nullptr;
    }

    void SceneStack::queue(Operation operation, Scene* scene, std::unique_ptr<Scene> owned) {
        pending.push_back({operation, {scene, std::move(owned)}});
    }

    void SceneStack::push(std::unique_ptr<Scene> scene) {
        Scene* raw = scene.get();
        queue(Operation::PUSH, raw, std::move(scene));
    }

    void SceneStack::push(Scene* scene) {
        queue(Operation::PUSH, scene, nullptr);
    }

    void SceneStack::replace(std::unique_ptr<Scene> scene) {
        Scene* raw = scene.get();
        queue(Operation::REPLACE, raw, std::move(scene));
    }

    void SceneStack::replace(Scene* scene) {
        queue(Operation::REPLACE, scene, nullptr);
    }

    void SceneStack::reset(std::unique_ptr<Scene> scene) {
        Scene* raw = scene.get();
        queue(Operation::RESET, raw, std::move(scene));
    }

    void SceneStack::reset(Scene* scene) {
        queue(Operation::RESET, scene, nullptr);
    }

    void SceneStack::pop() {
        queue(Operation::POP, nullptr, nullptr);
    }

    bool SceneStack::apply() {
        if (pending.empty()) {
            return false;
        }

        Scene* previous = top();
        std::vector<Entry> removed;

        for (Pending& change : pending) {
            bool removes = change.operation == Operation::POP || change.operation == Operation::REPLACE;

            if (change.operation == Operation::RESET) {
                while (!entries.empty()) {
                    removed.push_back(std::move(entries.back()));
                    entries.pop_back();
                }
            }
            else if (removes && !entries.empty()) {
                removed.push_back(std::move(entries.back()));
                entries.pop_back();
            }

            if (change.operation != Operation::POP && change.entry.scene != nullptr) {
                entries.push_back(std::move(change.entry));
            }
        }

        pending.clear();
        Scene* current = top();

        if (current == previous) {
            for (Entry& entry : removed) {
                if (entry.scene != current) {
                    destroy(entry);
                }
            }

            return false;
        }

        if (previous != nullptr) {
            previous->on_exit();
        }

        release_outgoing();

        for (Entry& entry : removed) {
            if (entry.scene == previous && !contains(previous)) {
                outgoing = std::move(entry);
            }
            else if (!contains(entry.scene)) {
                destroy(entry);
            }
        }

        if (outgoing.scene == nullptr && previous != nullptr) {
            outgoing.scene = previous;
        }

        transition_time = 0;
        skip_step = true;

        if (transition_duration <= 0) {
            release_outgoing();
        }

        if (current != nullptr) {
            current->attach(game);
            current->on_enter();
        }

        return true;
    }

    void SceneStack::release_outgoing() {
        if (outgoing.scene != nullptr && (outgoing.owned == nullptr || !contains(outgoing.scene))) {
            destroy(outgoing);
        }

        outgoing = Entry();
    }

    void SceneStack::advance(float elapsed_time) {
        if (outgoing.scene == nullptr) {
            return;
        }

        if (std::exchange(skip_step, false)) {
            return;
        }

        transition_time += std::min(elapsed_time, MAX_TRANSITION_STEP);

        if (transition_time >= transition_duration) {
            release_outgoing();
        }
    }

    void SceneStack::clear() {
        if (Scene* current = top()) {
            current->on_exit();
        }

        release_outgoing();

        while (!entries.empty()) {
            destroy(entries.back());
            entries.pop_back();
        }

        pending.clear();
    }

    Scene* SceneStack::top() const {
        return entries.empty() ? nullptr : entries.back().scene;
    }

    Scene* SceneStack::outgoing_scene() const {
        return outgoing.scene;
    }

    size_t SceneStack::depth() const {
        return entries.size();
    }

    bool SceneStack::contains(const Scene* scene) const {
        return scene != nullptr && std::any_of(entries.begin(), entries.end(), [&](const Entry& entry) { return entry.scene == scene; });
    }

    bool SceneStack::is_transitioning() const {
        return outgoing.scene != nullptr;
    }

    float SceneStack::transition_progress() const {
        return outgoing.scene == nullptr || transition_duration <= 0 ? 1.0f : std::clamp(transition_time / transition_duration, 0.0f, 1.0f);
    }

    void SceneStack::set_transition_duration(float seconds) {
        transition_duration = std::max(0.0f, seconds);
    }

    void SceneStack::for_each_created(const std::function<void(Scene*)>& visit) const {
        std::vector<Scene*> seen;

        auto offer = [&](Scene* scene) {
            if (scene != nullptr && scene->is_created() && std::find(seen.begin(), seen.end(), scene) == seen.end()) {
                seen.push_back(scene);
                visit(scene);
            }
        };

        for (const Entry& entry : entries) {
            offer(entry.scene);
        }

        offer(outgoing.scene);
    }
}
