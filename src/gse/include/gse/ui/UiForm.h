#pragma once

#include "gse/ui/KeyRepeat.h"
#include "gse/ui/UiField.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace nascent {
    class UiForm {
        public:
        enum class Action {
            NONE,
            MOVED,
            CHANGED,
            SUBMIT
        };

        protected:
        std::vector<std::unique_ptr<UiField>> fields;
        size_t focus = 0;
        KeyRepeat up_repeat;
        KeyRepeat down_repeat;
        bool enabled = true;
        mutable std::vector<int32_t> field_tops;
        mutable std::vector<int32_t> field_bottoms;
        mutable olc::vi2d origin = {0, 0};
        std::optional<std::pair<int32_t, int32_t>> clip;
        std::optional<size_t> dragging;

        std::optional<size_t> step_focus(size_t from, bool forward, bool wrap) const;
        std::optional<size_t> field_at(olc::vf2d point) const;
        olc::vf2d field_origin(size_t index) const;
        std::optional<Action> update_mouse(Input& input);

        public:
        std::optional<double> spacing;
        olc::Key reverse_modifier = olc::Key::SHIFT;
        olc::Key blocking_modifier = olc::Key::CTRL;

        template <typename T, typename... Args>
        T& add(Args&&... args) {
            fields.push_back(std::make_unique<T>(std::forward<Args>(args)...));
            return static_cast<T&>(*fields.back());
        }

        void clear();
        void focus_field(size_t index);
        void focus_nearest(int32_t y);
        void set_enabled(bool enabled);
        bool is_enabled() const;
        bool is_empty() const;
        const UiField* first_unfilled() const;
        const UiField* focused_field() const;
        UiField* focused_field();
        void set_clip(int32_t top, int32_t bottom);
        bool contains(olc::vf2d point) const;
        int32_t focused_top() const;
        int32_t focused_bottom() const;
        int32_t height() const;

        Action update(Input& input, float elapsed_time);
        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position) const;
    };
}
