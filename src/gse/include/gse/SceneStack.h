#pragma once

#include "gse/Scene.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace nascent {
    class Game;

    class SceneStack {
        protected:
        struct Entry {
            Scene* scene = nullptr;
            std::unique_ptr<Scene> owned;
        };

        enum class Operation {
            PUSH,
            POP,
            REPLACE,
            RESET
        };

        struct Pending {
            Operation operation;
            Entry entry;
        };

        Game* game = nullptr;
        std::vector<Entry> entries;
        std::vector<Pending> pending;
        Entry outgoing;
        float transition_time = 0;
        float transition_duration = 0;
        bool skip_step = false;

        void queue(Operation operation, Scene* scene, std::unique_ptr<Scene> owned);
        void release_outgoing();
        static void destroy(Entry& entry);

        public:
        static constexpr float MAX_TRANSITION_STEP = 1.0f / 30.0f;

        explicit SceneStack(Game* game);
        ~SceneStack();
        SceneStack(const SceneStack&) = delete;
        SceneStack& operator=(const SceneStack&) = delete;

        void push(std::unique_ptr<Scene> scene);
        void push(Scene* scene);
        void replace(std::unique_ptr<Scene> scene);
        void replace(Scene* scene);
        void reset(std::unique_ptr<Scene> scene);
        void reset(Scene* scene);
        void pop();

        bool apply();
        void advance(float elapsed_time);
        void clear();

        Scene* top() const;
        Scene* outgoing_scene() const;
        size_t depth() const;
        bool contains(const Scene* scene) const;
        bool is_transitioning() const;
        float transition_progress() const;
        void set_transition_duration(float seconds);
        void for_each_created(const std::function<void(Scene*)>& visit) const;
    };
}
