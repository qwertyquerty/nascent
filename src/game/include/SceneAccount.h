#pragma once

#include "EntityFFT.h"
#include "gse/Scene.h"
#include "ServerClient.h"
#include "gse/ui/Layout.h"
#include "gse/ui/UiForm.h"
#include "olcPixelGameEngine.h"

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class EntityStarfield;
    class Skin;
    class ButtonField;
    class TextField;

    class SceneAccount : public Scene {
        protected:
        Hotspots footer_spots;

        bool update_footer(Input& input);

        enum class Mode {
            SIGN_IN,
            REGISTER,
            SIGNED_IN
        };

        Skin* skin;
        Scene* next_scene;
        EntityStarfield* sky;
        bool pan_in;

        Mode mode = Mode::SIGN_IN;
        UiForm form;
        TextField* login_field = nullptr;
        TextField* email_field = nullptr;
        TextField* password_field = nullptr;
        ButtonField* continue_button = nullptr;
        ButtonField* profile_button = nullptr;
        ButtonField* sign_out_button = nullptr;
        ButtonField* switch_button = nullptr;
        ButtonField* forgot_button = nullptr;
        ButtonField* terms_button = nullptr;

        std::unique_ptr<EntityFFT> fft;
        std::vector<float> audio_buffer;

        std::future<ServerResponse> pending;
        std::string status;
        bool status_is_error = false;

        void set_mode(Mode mode);
        void submit();
        void finish(const ServerResponse& response);
        void leave();
        void sign_out();
        void open_profile();
        void open_password_reset();
        void open_terms();
        void play_menu_sound();

        public:
        SceneAccount(Skin* skin, Scene* next_scene, EntityStarfield* sky, bool pan_in = false);
        void on_create() override;

        static constexpr int32_t SPOT_CONFIRM = 0;
        static constexpr int32_t SPOT_SWITCH = 1;
        static constexpr int32_t SPOT_BACK = 2;
        static constexpr int32_t SPOT_SUBMIT = 3;
        static constexpr float FOOTER_GAP = 48.0f;
        void on_enter() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
