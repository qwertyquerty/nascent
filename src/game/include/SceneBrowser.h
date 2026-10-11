#pragma once

#include "Scene.h"
#include "ServerApi.h"
#include "UiForm.h"
#include "olcPixelGameEngine.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class ChoiceField;
    class NumberField;
    class SceneList;
    class Skin;
    class TextField;

    class SceneBrowser : public Scene {
        protected:
        Skin* skin;
        SceneList* list;

        UiForm form;
        TextField* search_field = nullptr;
        ChoiceField* key_field = nullptr;
        ChoiceField* status_field = nullptr;
        ChoiceField* downloadable_field = nullptr;
        NumberField* min_difficulty_field = nullptr;
        NumberField* max_difficulty_field = nullptr;
        NumberField* min_length_field = nullptr;
        NumberField* max_length_field = nullptr;

        bool results_focused = false;
        std::vector<ServerChart> results;
        int64_t total = 0;
        size_t selected = 0;
        float scroll = 0;

        ChartQuery wanted;
        std::optional<ChartQuery> shown;
        std::optional<ChartQuery> requested;
        float query_delay = 0;
        std::future<ApiResult<ChartList>> pending;
        std::string error;

        ChartQuery build_query() const;
        void request_page(const ChartQuery& query);
        void poll_results();
        bool download_selected();
        void focus_results(bool focused);
        void play_menu_sound();

        public:
        static constexpr int32_t PAGE_SIZE = 50;
        static constexpr int32_t ROW_HEIGHT = 76;
        static constexpr float QUERY_DELAY = 0.35f;
        static constexpr size_t PREFETCH_ROWS = 10;

        SceneBrowser(Skin* skin, SceneList* list);
        void init(Game*) override;
        void on_enter() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
