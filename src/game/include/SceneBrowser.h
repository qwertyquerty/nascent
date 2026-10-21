#pragma once

#include "BeatmapDownloads.h"
#include "gse/Scene.h"
#include "ServerApi.h"
#include "gse/ui/KeyRepeat.h"
#include "gse/ui/Layout.h"
#include "gse/ui/UiForm.h"
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
        ChoiceField* source_field = nullptr;
        ChoiceField* sort_field = nullptr;
        NumberField* min_difficulty_field = nullptr;
        NumberField* max_difficulty_field = nullptr;
        NumberField* min_length_field = nullptr;
        NumberField* max_length_field = nullptr;

        bool results_focused = false;
        std::vector<ServerChart> results;
        size_t fetched = 0;
        int64_t total = 0;
        size_t selected = 0;
        KeyRepeat up_repeat;
        KeyRepeat down_repeat;
        Hotspots result_spots;
        Hotspots hint_spots;

        bool update_mouse(Input& input);
        float scroll = 0;

        ChartQuery wanted;
        std::optional<ChartQuery> shown;
        std::optional<ChartQuery> requested;
        float query_delay = 0;
        std::future<ApiResult<ChartList>> pending;
        std::string error;
        std::string notice;
        std::future<BeatmapDownloads::PackLookup> pack_lookup;
        std::string pack_lookup_hash;

        ChartQuery build_query() const;
        void request_page(const ChartQuery& query);
        void poll_results();
        void poll_pack_lookup();
        bool download_selected();
        bool is_downloading(size_t index) const;
        bool cancel_download(size_t index);
        void focus_results(bool focused);
        void play_menu_sound();

        public:
        static constexpr int32_t PAGE_SIZE = 50;
        static constexpr float QUERY_DELAY = 0.35f;
        static constexpr size_t PREFETCH_ROWS = 10;

        SceneBrowser(Skin* skin, SceneList* list);
        void on_create() override;

        static constexpr int32_t HINT_BACK = 0;
        static constexpr int32_t HINT_CONFIRM = 1;
        static constexpr int32_t HINT_SWITCH = 2;
        void on_enter() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
