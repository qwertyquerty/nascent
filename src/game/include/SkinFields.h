#pragma once

#include "SkinSettings.h"
#include "gse/ui/RowField.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Skin;

    class SkinFollowing {
        public:
        bool follows_skin = true;

        virtual ~SkinFollowing() = default;
        virtual void refresh_skin(const Skin& skin) = 0;
        virtual bool use_skin_value() = 0;
        virtual void store(Settings& settings) const = 0;
    };

    class SkinSliderField : public SliderField, public SkinFollowing {
        protected:
        int32_t skin_value = 0;

        std::string display_value(bool focused) const override;
        virtual double skin_default(const Skin& skin) const;
        void load_saved(const Skin& skin);

        SkinSliderField(std::string label, SkinSetting setting, int32_t minimum, int32_t maximum, int32_t step, std::string unit);

        public:
        SkinSetting setting;

        SkinSliderField(std::string label, SkinSetting setting, const Skin& skin, int32_t minimum, int32_t maximum, int32_t step, std::string unit);

        void refresh_skin(const Skin& skin) override;
        bool use_skin_value() override;
        void store(Settings& settings) const override;

        bool update(Input& input, float elapsed_time) override;
        bool click(olc::vf2d point, Input& input) override;
        bool drag(olc::vf2d point) override;
    };

    class LaneGapField : public SkinSliderField {
        protected:
        uint8_t key_count;

        double skin_default(const Skin& skin) const override;

        public:
        LaneGapField(std::string label, const Skin& skin, uint8_t key_count, int32_t maximum);
    };

    class SkinChoiceField : public ChoiceField, public SkinFollowing {
        protected:
        std::vector<std::string> plain_labels;
        std::vector<std::string> skin_labels;
        bool skin_value = false;

        void show_labels();

        public:
        SkinToggle toggle;

        SkinChoiceField(std::string label, SkinToggle toggle, const Skin& skin, std::string off_label, std::string on_label);

        void refresh_skin(const Skin& skin) override;
        bool use_skin_value() override;
        void store(Settings& settings) const override;

        bool update(Input& input, float elapsed_time) override;
        bool click(olc::vf2d point, Input& input) override;
    };
}
