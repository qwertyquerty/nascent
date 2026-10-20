#pragma once

#include "gse/ui/KeyRepeat.h"
#include "gse/ui/UiField.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class RowField : public UiField {
        protected:
        float blink_timer = 0;
        mutable int32_t value_left = 0;
        mutable int32_t value_right = 0;

        virtual std::string display_value(bool focused) const = 0;
        virtual olc::Pixel value_color(UiTheme& theme, bool focused) const;
        virtual bool is_placeholder() const;
        virtual bool shows_caret() const;

        public:
        static int32_t width();
        static int32_t height();

        std::string label;

        RowField(std::string label);

        void on_focus() override;
        bool update(Input& input, float elapsed_time) override;
        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const override;
    };

    class ChoiceField : public RowField {
        protected:
        mutable float previous_left = 0;
        mutable float previous_right = 0;

        std::string display_value(bool focused) const override;
        const std::string& current() const;

        public:
        std::vector<std::string> options;
        size_t selected = 0;

        ChoiceField(std::string label, std::vector<std::string> options, size_t selected = 0);

        bool update(Input& input, float elapsed_time) override;
        bool click(olc::vf2d point, Input& input) override;
        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const override;
    };

    class SliderField : public RowField {
        protected:
        KeyRepeat left_repeat;
        KeyRepeat right_repeat;
        bool dragging_bar = false;

        bool set_from_point(float x);

        std::string display_value(bool focused) const override;

        public:
        int32_t value;
        int32_t minimum;
        int32_t maximum;
        int32_t step;
        std::string unit;

        SliderField(std::string label, int32_t value, int32_t minimum, int32_t maximum, int32_t step, std::string unit);

        bool update(Input& input, float elapsed_time) override;
        bool click(olc::vf2d point, Input& input) override;
        bool drag(olc::vf2d point) override;
        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const override;
    };

    class ButtonField : public RowField {
        protected:
        bool pressed = false;
        bool consumed = false;

        std::string display_value(bool focused) const override;

        public:
        ButtonField(std::string label);

        bool update(Input& input, float elapsed_time) override;
        bool click(olc::vf2d point, Input& input) override;
        bool consumes_navigation() const override;
        bool take_pressed();
    };

    class SectionField : public UiField {
        public:
        std::string title;

        SectionField(std::string title);

        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const override;
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

        bool update(Input& input, float elapsed_time) override;
        std::optional<double> value() const;
    };
}
