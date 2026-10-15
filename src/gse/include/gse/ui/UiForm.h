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
        protected:
        std::vector<std::unique_ptr<UiField>> fields;
        size_t focus = 0;
        KeyRepeat up_repeat;
        KeyRepeat down_repeat;
        bool enabled = true;
        mutable std::vector<int32_t> field_tops;
        mutable std::vector<int32_t> field_bottoms;

        std::optional<size_t> step_focus(size_t from, bool forward, bool wrap) const;

        public:
        static constexpr int32_t FIELD_SPACING = 28;

        int32_t spacing = FIELD_SPACING;
        olc::Key reverse_modifier = olc::Key::SHIFT;
        olc::Key blocking_modifier = olc::Key::CTRL;

        enum class Action {
            NONE,
            MOVED,
            CHANGED,
            SUBMIT
        };

        template <typename T, typename... Args>
        T& add(Args&&... args) {
            fields.push_back(std::make_unique<T>(std::forward<Args>(args)...));
            return static_cast<T&>(*fields.back());
        }

        void clear();
        void focus_field(size_t index);
        void set_enabled(bool enabled);
        bool is_enabled() const;
        bool is_empty() const;
        const UiField* first_unfilled() const;
        const UiField* focused_field() const;
        int32_t focused_top() const;
        int32_t focused_bottom() const;
        int32_t height() const;

        Action update(Input& input, float elapsed_time);
        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position) const;
    };
}
