#include "SceneAccount.h"

#include "Account.h"
#include "Game.h"
#include "InputManager.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "TextField.h"

#include <chrono>
#include <format>

namespace nascent {
    static constexpr double FFT_HEIGHT_RATIO = 0.28;

    SceneAccount::SceneAccount(Skin* skin, Scene* next_scene) {
        this->skin = skin;
        this->next_scene = next_scene;
    }

    void SceneAccount::init(Game*) {
        olc::vi2d window_size = game->window->GetWindowSize();
        double height = (double)window_size.y * FFT_HEIGHT_RATIO;

        fft = std::make_unique<EntityFFT>(skin, olc::vd2d{0, (double)window_size.y - height}, olc::vd2d{(double)window_size.x, height}, 0);
        fft->flipped = true;
        fft->init(this);
    }

    void SceneAccount::on_enter() {
        game->play_sound(skin->account_loop_sfx, true);
        status.clear();
        set_mode(Account::signed_in() ? Mode::SIGNED_IN : Mode::SIGN_IN);
    }

    void SceneAccount::on_exit() {
        form.set_enabled(game->window, false);
        game->stop_sound(skin->account_loop_sfx);
    }

    void SceneAccount::play_menu_sound() {
        game->play_sound(skin->get_random_menu_pluck());
    }

    void SceneAccount::set_mode(Mode mode) {
        this->mode = mode;
        form.clear(game->window);
        login_field = email_field = password_field = nullptr;

        if (mode == Mode::SIGNED_IN) {
            return;
        }

        login_field = &form.add<TextField>(mode == Mode::SIGN_IN ? "Username or email" : "Username");

        if (mode == Mode::REGISTER) {
            email_field = &form.add<TextField>("Email");
        }

        password_field = &form.add<TextField>("Password", "", true);
        form.focus_field(game->window, 0);
    }

    void SceneAccount::submit() {
        if (const UiField* empty = form.first_unfilled()) {
            status = std::format("{} is empty", static_cast<const TextField*>(empty)->label);
            status_is_error = true;
            return;
        }

        form.set_enabled(game->window, false);
        status_is_error = false;

        if (mode == Mode::REGISTER) {
            status = "Creating your account...";
            pending = Account::create(login_field->value, email_field->value, password_field->value);
        }
        else {
            status = "Signing in...";
            pending = Account::sign_in(login_field->value, password_field->value);
        }
    }

    void SceneAccount::finish(const ServerResponse& response) {
        if (response.ok()) {
            password_field->value.clear();
            leave();
            return;
        }

        status = response.message();
        status_is_error = true;
        form.set_enabled(game->window, true);
    }

    void SceneAccount::leave() {
        play_menu_sound();
        game->set_scene(next_scene);
    }

    void SceneAccount::update(float elapsed_time) {
        olc::PixelGameEngine* window = game->window;

        if (skin->account_loop_sfx != -1 && game->get_audio().IsPlaying(skin->account_loop_sfx)) {
            game->get_audio().CopyEngineBuffer(audio_buffer);
            fft->update_song_buffer(audio_buffer, elapsed_time);
        }

        if (pending.valid()) {
            if (pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                finish(pending.get());
            }

            return;
        }

        if (mode == Mode::SIGNED_IN) {
            if (window->GetKey(InputManager::menu_start_key).bPressed) {
                leave();
            }
            else if (InputManager::switch_pressed(window)) {
                play_menu_sound();
                Account::sign_out();
                status = "Signed out";
                status_is_error = false;
                set_mode(Mode::SIGN_IN);
            }

            return;
        }

        UiForm::Action action = form.update(window, game->get_frame_text(), elapsed_time);

        if (InputManager::switch_pressed(window)) {
            play_menu_sound();
            status.clear();
            set_mode(mode == Mode::SIGN_IN ? Mode::REGISTER : Mode::SIGN_IN);
        }
        else if (action == UiForm::Action::SUBMIT) {
            play_menu_sound();
            submit();
        }
        else if (action == UiForm::Action::MOVED) {
            game->play_sound(skin->hitsound_sfx);
        }
        else if (action == UiForm::Action::CHANGED) {
            play_menu_sound();
        }
    }

    void SceneAccount::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        fft->draw(window);

        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        const char* title = mode == Mode::SIGN_IN ? "Sign in" : mode == Mode::REGISTER ? "Create an account"
                                                                                       : "Account";
        skin->display_font_large.DrawString(title, {x, y}, olc::WHITE);
        y += 90;

        skin->display_font_small.DrawString("Signing in lets your plays count towards the leaderboards.", {x, y}, olc::GREY);
        y += 26;
        skin->display_font_small.DrawString(SettingsManager::settings.server_url, {x, y}, olc::DARK_GREY);
        y += 60;

        if (mode == Mode::SIGNED_IN) {
            skin->display_font_medium.DrawString(std::format("Signed in as {}", Account::username()), {x, y}, olc::WHITE);
            y += 60;
        }

        y += form.draw(window, skin, {x, y});

        if (!status.empty()) {
            skin->display_font_small.DrawString(status, {x, y}, status_is_error ? olc::RED : olc::GREY);
        }

        std::string footer;

        if (mode == Mode::SIGNED_IN) {
            footer = "Enter:  continue        Ctrl+Tab:  sign out        Esc:  back";
        }
        else {
            footer = std::format(
                "Tab:  field        Enter:  {}        Ctrl+Tab:  {}        Esc:  play offline",
                mode == Mode::SIGN_IN ? "sign in" : "create account",
                mode == Mode::SIGN_IN ? "create an account" : "sign in instead"
            );
        }

        olc::FontRect footer_bounds = skin->display_font_small.GetStringBounds(footer);
        int32_t footer_bottom = screen.y - screen.y / 12;
        skin->display_font_small.DrawString(footer, {x, footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y)}, olc::GREY);
    }
}
