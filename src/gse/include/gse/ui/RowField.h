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

        virtual std::string display_value(bool focused) const = 0;
        virtual olc::Pixel value_color(UiTheme& theme, bool focused) const;
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

        void on_focus() override;
        bool update(Input& input, float elapsed_time) override;
        int32_t draw(olc::PixelGameEngine* window, UiTheme& theme, olc::vi2d position, bool focused) const override;
    };

    class ChoiceField : public RowField {
        protected:
        std::string display_value(bool focused) const override;

        public:
        std::vector<std::string> options;
        size_t selected = 0;

        ChoiceField(std::string label, std::vector<std::string> options, size_t selected = 0);

        bool update(Input& input, float elapsed_time) override;
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

        bool update(Input& input, float elapsed_time) override;
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
        bool consumes_navigation() const override;
        bool take_pressed();
    };

    class SectionField : public UiField {
        public:
        static constexpr int32_t TOP_GAP = 24;
        static constexpr int32_t LINE_GAP = 10;
        static constexpr int32_t LINE_HEIGHT = 2;

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
