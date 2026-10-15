#pragma once

#include "InputManager.h"
#include "gse/ui/RowField.h"

#include <optional>
#include <string>

namespace nascent {
    class KeyBindField : public RowField {
        protected:
        bool capturing = false;
        bool consumed = false;

        std::string display_value(bool focused) const override;
        olc::Pixel value_color(UiTheme& theme, bool focused) const override;

        public:
        static constexpr float CAPTURE_PULSE_SPEED = 6.0f;

        KeyBinding binding;
        std::optional<KeyBinding> swapped;

        KeyBindField(KeyBinding binding);

        void on_blur() override;
        bool update(Input& input, float elapsed_time) override;
        bool consumes_navigation() const override;
        bool is_capturing() const;
    };
}
