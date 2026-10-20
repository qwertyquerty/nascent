#pragma once

#include "gse/Scene.h"
#include "ServerApi.h"
#include "gse/ui/Layout.h"
#include "gse/ui/ScrollBar.h"
#include "gse/ui/UiForm.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <filesystem>
#include <future>
#include <optional>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class Attempt;
    class ButtonField;
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
        ChoiceField* note_gradient_field = nullptr;
        ChoiceField* judgement_line_field = nullptr;
        ChoiceField* frame_rate_field = nullptr;
        ChoiceField* fps_counter_field = nullptr;
        ChoiceField* rich_presence_field = nullptr;
        ChoiceField* transition_field = nullptr;
        ChoiceField* title_music_field = nullptr;
        ChoiceField* fullscreen_field = nullptr;
        ChoiceField* mirror_field = nullptr;
        ChoiceField* instant_restart_field = nullptr;
        ChoiceField* linked_folders_field = nullptr;
        ButtonField* link_folder_button = nullptr;
        ButtonField* export_skin_button = nullptr;
        ButtonField* delete_skin_button = nullptr;
        bool delete_confirming = false;
        std::future<std::string> skin_export;
        std::string exported_skin;
        ButtonField* link_osu_button = nullptr;
        std::optional<std::filesystem::path> osu_songs;
        std::future<std::optional<std::filesystem::path>> folder_pick;
        bool unlink_requested = false;
        SliderField* lane_cover_field = nullptr;
        ChoiceField* hitsounds_field = nullptr;
        SliderField* lane_width_field = nullptr;
        SliderField* background_brightness_field = nullptr;
        SliderField* note_opacity_field = nullptr;
        SliderField* receptor_opacity_field = nullptr;
        ChoiceField* lane_gap_field = nullptr;
        std::vector<int16_t> lane_gaps;
        SliderField* judge_offset_field = nullptr;
        ChoiceField* scroll_direction_field = nullptr;
        ChoiceField* judgement_field = nullptr;
        SliderField* judgement_height_field = nullptr;
        ChoiceField* combo_field = nullptr;
        SliderField* combo_height_field = nullptr;
        ChoiceField* accuracy_field = nullptr;
        ChoiceField* average_error_field = nullptr;
        ChoiceField* error_deviation_field = nullptr;
        ChoiceField* performance_field = nullptr;
        ChoiceField* timing_bar_field = nullptr;
        SliderField* timing_bar_height_field = nullptr;
        SliderField* timing_bar_length_field = nullptr;
        SliderField* timing_bar_width_field = nullptr;
        SliderField* master_volume_field = nullptr;
        SliderField* music_volume_field = nullptr;
        SliderField* effects_volume_field = nullptr;
        std::vector<KeyBindField*> key_fields;
        TextField* server_field = nullptr;
        ButtonField* data_folder_button = nullptr;
        SliderField* visual_offset_field = nullptr;
        SliderField* input_offset_field = nullptr;
        SliderField* scroll_speed_field = nullptr;
        ButtonField* calibration_button = nullptr;
        std::string status;

        std::string server_typed;
        std::string server_checked;
        std::string server_checking;
        float server_check_delay = 0;
        std::future<ApiResult<ServerHealth>> server_check;

        float scroll = 0;
        float scroll_target = 0;
        Hotspots footer_spots;
        ScrollBar scrollbar;
        int32_t viewport_top = 0;
        int32_t viewport_bottom = 0;

        std::unique_ptr<Chart> preview_chart;
        std::unique_ptr<EntityField> preview_field;
        std::unique_ptr<Attempt> preview_attempt;
        double preview_time = 0;
        double preview_position = 0;

        std::vector<uint16_t> frame_rates;
        std::vector<SkinInfo> skins;
        uint32_t skin_list_version = 0;
        bool changed = false;

        void apply();
        void update_server(float elapsed_time);
        void commit_server();
        void update_scroll(float elapsed_time);
        float scroll_position() const;
        void scroll_to(float position);
        void build_preview_chart();
        void rebuild_preview();
        void refresh_skins();
        void refresh_linked_folders();
        void link_folder(const std::filesystem::path& folder);
        void update_linked_folders();
        void export_selected_skin();
        void report_skin_problems();
        void delete_selected_skin();
        void update_skin_actions();
        void update_preview(float elapsed_time);
        std::vector<Hint> footer_hints() const;
        bool update_footer(Input& input);
        void draw_scrollbar(olc::PixelGameEngine* window, int32_t x);

        static constexpr int32_t FOOTER_BACK = 0;
        static constexpr int32_t FOOTER_CONFIRM = 1;
        static constexpr int32_t FOOTER_RESET = 2;

        public:
        SceneOptions(Skin* skin);
        ~SceneOptions() override;
        void on_create() override;
        void on_enter() override;
        void on_exit() override;
        void on_resize() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
