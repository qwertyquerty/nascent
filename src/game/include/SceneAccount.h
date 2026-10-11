#pragma once

#include "EntityFFT.h"
#include "EntityStarfield.h"
#include "Scene.h"
#include "ServerClient.h"
#include "UiForm.h"
#include "olcPixelGameEngine.h"

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace nascent {
    class Skin;
    class TextField;

    class SceneAccount : public Scene {
        protected:
        enum class Mode {
            SIGN_IN,
            REGISTER,
            SIGNED_IN
        };

        Skin* skin;
        Scene* next_scene;

        Mode mode = Mode::SIGN_IN;
        UiForm form;
        TextField* login_field = nullptr;
        TextField* email_field = nullptr;
        TextField* password_field = nullptr;

        std::unique_ptr<EntityStarfield> starfield;
        std::unique_ptr<EntityFFT> fft;
        std::vector<float> audio_buffer;

        std::future<ServerResponse> pending;
        std::string status;
        bool status_is_error = false;

        void set_mode(Mode mode);
        void submit();
        void finish(const ServerResponse& response);
        void leave();
        void play_menu_sound();

        public:
        SceneAccount(Skin* skin, Scene* next_scene);
        void init(Game*) override;
        void on_enter() override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}
