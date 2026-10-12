#pragma once

#include "Scene.h"
#include "UiForm.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <vector>

namespace nascent {
    class ChoiceField;
    class Skin;

    class SceneOptions : public Scene {
        protected:
        Skin* skin;
        UiForm form;

        ChoiceField* frame_rate_field = nullptr;
        ChoiceField* fps_counter_field = nullptr;
        ChoiceField* transition_field = nullptr;

        std::vector<uint16_t> frame_rates;
        bool changed = false;

        void apply();

        public:
        SceneOptions(Skin* skin);
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
