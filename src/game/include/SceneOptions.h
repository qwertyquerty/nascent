#pragma once

#include "Scene.h"
#include "UiForm.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class Attempt;
    class Chart;
    class ChoiceField;
    class EntityField;
    class SliderField;
    class KeyBindField;
    class Skin;
    struct SkinInfo;

    class SceneOptions : public Scene {
        protected:
        Skin* skin;
        UiForm form;

        ChoiceField* skin_field = nullptr;
        ChoiceField* frame_rate_field = nullptr;
        ChoiceField* fps_counter_field = nullptr;
        ChoiceField* transition_field = nullptr;
        SliderField* lane_width_field = nullptr;
        ChoiceField* lane_gap_field = nullptr;
        std::vector<int16_t> lane_gaps;
        SliderField* judge_offset_field = nullptr;
        ChoiceField* scroll_direction_field = nullptr;
        SliderField* master_volume_field = nullptr;
        SliderField* music_volume_field = nullptr;
        SliderField* effects_volume_field = nullptr;
        std::vector<KeyBindField*> key_fields;
        std::string status;

        float scroll = 0;
        float scroll_target = 0;
        int32_t viewport_top = 0;
        int32_t viewport_bottom = 0;

        std::unique_ptr<Chart> preview_chart;
        std::unique_ptr<EntityField> preview_field;
        std::unique_ptr<Attempt> preview_attempt;
        double preview_time = 0;
        double preview_position = 0;

        std::vector<uint16_t> frame_rates;
        std::vector<SkinInfo> skins;
        bool changed = false;

        void apply();
        void update_scroll(float elapsed_time);
        void build_preview_chart();
        void rebuild_preview();
        void update_preview(float elapsed_time);
        std::string footer_text() const;
        void draw_scrollbar(olc::PixelGameEngine* window, int32_t x) const;

        public:
        SceneOptions(Skin* skin);
        ~SceneOptions() override;
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
