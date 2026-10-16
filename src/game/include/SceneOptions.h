#pragma once

#include "gse/Scene.h"
#include "ServerApi.h"
#include "gse/ui/Layout.h"
#include "gse/ui/UiForm.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class Attempt;
    class Chart;
    class ChoiceField;
    class EntityField;
    class SliderField;
    class TextField;
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
        ChoiceField* rich_presence_field = nullptr;
        ChoiceField* transition_field = nullptr;
        ChoiceField* fullscreen_field = nullptr;
        ChoiceField* mirror_field = nullptr;
        ChoiceField* hitsounds_field = nullptr;
        SliderField* lane_width_field = nullptr;
        SliderField* background_brightness_field = nullptr;
        ChoiceField* lane_gap_field = nullptr;
        std::vector<int16_t> lane_gaps;
        SliderField* judge_offset_field = nullptr;
        ChoiceField* scroll_direction_field = nullptr;
        ChoiceField* judgement_field = nullptr;
        SliderField* judgement_height_field = nullptr;
        ChoiceField* combo_field = nullptr;
        SliderField* combo_height_field = nullptr;
        ChoiceField* timing_bar_field = nullptr;
        SliderField* timing_bar_height_field = nullptr;
        SliderField* timing_bar_length_field = nullptr;
        SliderField* timing_bar_width_field = nullptr;
        SliderField* master_volume_field = nullptr;
        SliderField* music_volume_field = nullptr;
        SliderField* effects_volume_field = nullptr;
        std::vector<KeyBindField*> key_fields;
        TextField* server_field = nullptr;
        std::string status;

        std::string server_typed;
        std::string server_checked;
        std::string server_checking;
        float server_check_delay = 0;
        std::future<ApiResult<ServerHealth>> server_check;

        float scroll = 0;
        float scroll_target = 0;
        Hotspots footer_spots;
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
        void update_server(float elapsed_time);
        void commit_server();
        void update_scroll(float elapsed_time);
        void build_preview_chart();
        void rebuild_preview();
        void update_preview(float elapsed_time);
        std::vector<Hint> footer_hints() const;
        bool update_footer(Input& input);
        void draw_scrollbar(olc::PixelGameEngine* window, int32_t x) const;

        static constexpr int32_t FOOTER_BACK = 0;
        static constexpr int32_t FOOTER_CONFIRM = 1;
        static constexpr int32_t FOOTER_RESET = 2;
        static constexpr float FOOTER_HINT_GAP = 48.0f;

        public:
        SceneOptions(Skin* skin);
        ~SceneOptions() override;
        void on_create() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
