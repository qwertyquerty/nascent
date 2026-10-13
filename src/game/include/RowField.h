#pragma once

#include "InputManager.h"
#include "KeyRepeat.h"
#include "UiField.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class RowField : public UiField {
        protected:
        float blink_timer = 0;

        virtual std::string display_value(bool focused) const = 0;
        virtual olc::Pixel value_color(Skin* skin, bool focused) const;
        virtual bool is_placeholder() const;
        virtual bool shows_caret() const;

        public:
        static constexpr int32_t WIDTH = 640;
        static constexpr int32_t HEIGHT = 40;
        static constexpr int32_t PADDING = 14;

        static int32_t width();
        static int32_t height();

        std::string label;

        RowField(std::string label);

        void on_focus(olc::PixelGameEngine* window) override;
        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
        int32_t draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position, bool focused) const override;
    };

    class ChoiceField : public RowField {
        protected:
        std::string display_value(bool focused) const override;

        public:
        std::vector<std::string> options;
        size_t selected = 0;

        ChoiceField(std::string label, std::vector<std::string> options, size_t selected = 0);

        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
    };

    class SliderField : public RowField {
        protected:
        KeyRepeat left_repeat;
        KeyRepeat right_repeat;

        std::string display_value(bool focused) const override;

        public:
        static constexpr int32_t BAR_LEFT = 250;
        static constexpr int32_t BAR_RIGHT_GAP = 110;
        static constexpr int32_t BAR_HEIGHT = 6;

        int32_t value;
        int32_t minimum;
        int32_t maximum;
        int32_t step;
        std::string unit;

        SliderField(std::string label, int32_t value, int32_t minimum, int32_t maximum, int32_t step, std::string unit);

        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
        int32_t draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position, bool focused) const override;
    };

    class KeyBindField : public RowField {
        protected:
        bool capturing = false;
        bool consumed = false;

        std::string display_value(bool focused) const override;
        olc::Pixel value_color(Skin* skin, bool focused) const override;

        public:
        static constexpr float CAPTURE_PULSE_SPEED = 6.0f;

        KeyBinding binding;
        std::optional<KeyBinding> swapped;

        KeyBindField(KeyBinding binding);

        void on_blur(olc::PixelGameEngine* window) override;
        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
        bool consumes_navigation() const override;
        bool is_capturing() const;
    };

    class ButtonField : public RowField {
        protected:
        bool pressed = false;
        bool consumed = false;

        std::string display_value(bool focused) const override;

        public:
        ButtonField(std::string label);

        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
        bool consumes_navigation() const override;
        bool take_pressed();
    };

    class SectionField : public UiField {
        public:
        static constexpr int32_t TOP_GAP = 24;
        static constexpr int32_t LINE_GAP = 10;
        static constexpr int32_t LINE_HEIGHT = 2;
        static constexpr uint8_t LINE_ALPHA = 50;

        std::string title;

        SectionField(std::string title);

        int32_t draw(olc::PixelGameEngine* window, Skin* skin, olc::vi2d position, bool focused) const override;
        bool focusable() const override;
    };

    class NumberField : public RowField {
        protected:
        std::string display_value(bool focused) const override;
        bool is_placeholder() const override;
        bool shows_caret() const override;

        public:
        std::string text;
        std::string unit;
        double step;
        double maximum;

        NumberField(std::string label, std::string unit, double step, double maximum);

        bool update(olc::PixelGameEngine* window, const std::string& typed, float elapsed_time) override;
        std::optional<double> value() const;
    };
}
