#pragma once

#include "UiField.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace nascent {
    class UiForm {
        protected:
        std::vector<std::unique_ptr<UiField>> fields;
        size_t focus = 0;
        bool enabled = true;

        public:
        static constexpr int32_t FIELD_SPACING = 28;

        enum class Action {
            NONE,
            SUBMIT
        };

        template <typename T, typename... Args>
        T& add(Args&&... args) {
            fields.push_back(std::make_unique<T>(std::forward<Args>(args)...));
            return static_cast<T&>(*fields.back());
        }

        void clear(olc::PixelGameEngine* window);
        void focus_field(olc::PixelGameEngine* window, size_t index);
        void set_enabled(olc::PixelGameEngine* window, bool enabled);
        bool is_enabled() const;
        bool is_empty() const;
        const UiField* first_unfilled() const;

        Action update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time);
        int32_t draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position) const;
    };
}
