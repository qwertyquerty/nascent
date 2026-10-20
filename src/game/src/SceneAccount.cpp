#include "SceneAccount.h"
#include "RichPresence.h"

#include "Account.h"
#include "EntityStarfield.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "gse/ui/RowField.h"
#include "KeyBindField.h"
#include "gse/Platform.h"
#include "ServerApi.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "gse/ui/TextField.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "gse/concurrency/Async.h"

#include <chrono>
#include <format>

namespace nascent {
    SceneAccount::SceneAccount(Skin* skin, Scene* next_scene, EntityStarfield* sky, bool pan_in) {
        this->skin = skin;
        this->next_scene = next_scene;
        this->sky = sky;
        this->pan_in = pan_in;
    }

    void SceneAccount::on_create() {
        olc::vi2d window_size = game->window->GetScreenSize();
        double height = (double)window_size.y * Ui::layout("account.spectrum_height");

        fft = std::make_unique<EntityFFT>(skin, olc::vd2d{0, (double)window_size.y - height}, olc::vd2d{(double)window_size.x, height}, 0);
        fft->flipped = true;
        fft->init();
    }

    void SceneAccount::on_enter() {
        RichPresence::in_menu(Account::signed_in() ? "Managing their account" : "Signing in");
        if (pan_in) {
            sky->pan_to(SkyView::ACCOUNT);
        }
        else {
            sky->show(SkyView::ACCOUNT);
        }

        game->audio().play_music(skin->sound(SkinSound::ACCOUNT_LOOP), true);
        status.clear();
        Settings& settings = SettingsManager::settings;
        Mode initial = Account::signed_in() ? Mode::SIGNED_IN : settings.account_screen_seen ? Mode::SIGN_IN
                                                                                             : Mode::REGISTER;

        if (!settings.account_screen_seen) {
            settings.account_screen_seen = true;
            SettingsManager::save();
        }

        set_mode(initial);
    }

    void SceneAccount::on_exit() {
        form.set_enabled(false);
        game->audio().stop(skin->sound(SkinSound::ACCOUNT_LOOP));
    }

    void SceneAccount::play_menu_sound() {
        game->audio().play_effect(skin->get_random_menu_pluck());
    }

    void SceneAccount::set_mode(Mode mode) {
        this->mode = mode;
        form.clear();
        login_field = email_field = password_field = confirm_field = nullptr;
        continue_button = submit_button = profile_button = sign_out_button = switch_button = forgot_button = terms_button = nullptr;

        if (mode == Mode::SIGNED_IN) {
            continue_button = &form.add<ButtonField>("Continue");
            profile_button = &form.add<ButtonField>("Open profile on website");
            sign_out_button = &form.add<ButtonField>("Sign out");
            form.focus_field(0);
            return;
        }

        login_field = &form.add<TextField>(mode == Mode::SIGN_IN ? "Username or email" : "Username");

        if (mode == Mode::REGISTER) {
            email_field = &form.add<TextField>("Email");
        }

        password_field = &form.add<TextField>("Password", "", true);

        if (mode == Mode::REGISTER) {
            confirm_field = &form.add<TextField>("Retype password", "", true);
        }

        submit_button = &form.add<ButtonField>(mode == Mode::SIGN_IN ? "Sign in" : "Create account");
        switch_button = &form.add<ButtonField>(mode == Mode::SIGN_IN ? "No account yet? Create one" : "Already have an account? Sign in");

        if (mode == Mode::SIGN_IN) {
            forgot_button = &form.add<ButtonField>("Forgot password?");
        }
        else {
            terms_button = &form.add<ButtonField>("Terms and privacy policy");
        }

        form.focus_field(0);
    }

    void SceneAccount::submit() {
        if (const UiField* empty = form.first_unfilled()) {
            status = std::format("{} is empty", static_cast<const TextField*>(empty)->label);
            status_is_error = true;
            return;
        }

        if (confirm_field != nullptr && confirm_field->value != password_field->value) {
            status = "The passwords don't match";
            status_is_error = true;
            confirm_field->value.clear();
            return;
        }

        form.set_enabled(false);
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

            if (confirm_field != nullptr) {
                confirm_field->value.clear();
            }

            leave();
            return;
        }

        status = response.message();
        status_is_error = true;
        form.set_enabled(true);
    }

    void SceneAccount::leave() {
        play_menu_sound();
        if (next_scene != nullptr) {
            game->scenes().replace(next_scene);
        }
        else {
            game->scenes().pop();
        }
    }

    void SceneAccount::sign_out() {
        play_menu_sound();
        Account::sign_out();
        status = "Signed out";
        status_is_error = false;
        set_mode(Mode::SIGN_IN);
    }

    void SceneAccount::open_profile() {
        play_menu_sound();
        bool opened = open_url(ServerApi::profile_page_url(Account::username()));
        status = opened ? "Opened your profile in the browser" : "Could not open the browser";
        status_is_error = !opened;
    }

    void SceneAccount::open_password_reset() {
        play_menu_sound();
        bool opened = open_url(ServerApi::website_url("/forgot"));
        status = opened ? "Opened the password reset page in the browser" : "Could not open the browser";
        status_is_error = !opened;
    }

    void SceneAccount::open_terms() {
        play_menu_sound();
        bool opened = open_url(ServerApi::website_url("/terms"));
        status = opened ? "Opened the terms in the browser" : "Could not open the browser";
        status_is_error = !opened;
    }

    bool SceneAccount::update_footer(Input& input) {
        std::optional<int32_t> spot = footer_spots.take_click(input);

        if (!spot) {
            return false;
        }

        switch (*spot) {
            case SPOT_BACK:
                input.inject(actions::BACK);
                return false;
            case SPOT_CONFIRM:
                input.inject(actions::CONFIRM);
                return false;
            case SPOT_SUBMIT:
                play_menu_sound();
                submit();
                return true;
            default:
                if (mode == Mode::SIGNED_IN) {
                    sign_out();
                }
                else {
                    play_menu_sound();
                    status.clear();
                    set_mode(mode == Mode::SIGN_IN ? Mode::REGISTER : Mode::SIGN_IN);
                }

                return true;
        }
    }

    void SceneAccount::update(float elapsed_time) {
        Input& input = game->input();
        sky->update(elapsed_time);

        if (skin->sound(SkinSound::ACCOUNT_LOOP) != -1 && game->audio().engine().IsPlaying(skin->sound(SkinSound::ACCOUNT_LOOP))) {
            game->audio().engine().CopyEngineBuffer(audio_buffer);
            fft->update_song_buffer(audio_buffer, elapsed_time);
        }

        if (pending.valid()) {
            if (is_ready(pending)) {
                finish(pending.get());
            }

            return;
        }

        if (update_footer(input)) {
            return;
        }

        if (next_scene != nullptr && input.take(actions::BACK)) {
            leave();
            return;
        }

        if (mode == Mode::SIGNED_IN) {
            UiForm::Action action = form.update(input, elapsed_time);

            if (continue_button->take_pressed()) {
                leave();
            }
            else if (profile_button->take_pressed()) {
                open_profile();
            }
            else if (sign_out_button->take_pressed() || InputManager::switch_pressed(input)) {
                sign_out();
            }
            else if (action == UiForm::Action::MOVED) {
                game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            }

            return;
        }

        if (form.focused_field() == (confirm_field != nullptr ? confirm_field : password_field) && input.take(actions::CONFIRM)) {
            play_menu_sound();
            submit();
            return;
        }

        UiForm::Action action = form.update(input, elapsed_time);

        if (submit_button->take_pressed()) {
            play_menu_sound();
            submit();
        }
        else if (switch_button->take_pressed() || InputManager::switch_pressed(input)) {
            play_menu_sound();
            status.clear();
            set_mode(mode == Mode::SIGN_IN ? Mode::REGISTER : Mode::SIGN_IN);
        }
        else if (forgot_button != nullptr && forgot_button->take_pressed()) {
            open_password_reset();
        }
        else if (terms_button != nullptr && terms_button->take_pressed()) {
            open_terms();
        }
        else if (action == UiForm::Action::SUBMIT) {
            play_menu_sound();
            submit();
        }
        else if (action == UiForm::Action::MOVED) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }
        else if (action == UiForm::Action::CHANGED) {
            play_menu_sound();
        }
    }

    void SceneAccount::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        sky->draw(window);
        fft->draw(window);

        int32_t x = (int32_t)(screen.x * Ui::layout("account.left"));
        int32_t y = (int32_t)(screen.y * Ui::layout("account.top"));

        const char* title = mode == Mode::SIGN_IN ? "Sign in" : mode == Mode::REGISTER ? "Create an account"
                                                                                       : "Account";
        draw_text(skin->display_font_large, title, {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::layout_px("account.subtitle_offset");

        const char* subtitle = mode == Mode::REGISTER ? "Creating an account means you agree to the terms and privacy policy." : "Signing in lets your plays count towards the leaderboards.";
        draw_text(skin->display_font_small, subtitle, {x, y}, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::layout_px("account.server_offset");
        draw_text(skin->display_font_small, SettingsManager::settings.server_url, {x, y}, skin->color(SkinColor::TEXT_FAINT));
        y += Ui::layout_px("account.form_offset");

        if (mode == Mode::SIGNED_IN) {
            draw_text(skin->display_font_medium, std::format("Signed in as {}", Account::username()), {x, y}, skin->color(SkinColor::TEXT));
            y += Ui::layout_px("account.signed_in_offset");
        }

        y += form.draw(window, *skin, {x, y});

        if (!status.empty()) {
            draw_text(skin->display_font_small, status, {x, y}, skin->color(status_is_error ? SkinColor::TEXT_ERROR : SkinColor::TEXT_MUTED));
        }

        std::vector<Hint> footer;

        if (mode == Mode::SIGNED_IN) {
            footer = {{"↑/↓:  choose"}, {"Enter:  select", SPOT_CONFIRM}, {"Ctrl+Tab:  sign out", SPOT_SWITCH}, {"Esc:  back", SPOT_BACK}};
        }
        else {
            footer = {
                {"Tab:  field"},
                {std::format("Enter:  {}", mode == Mode::SIGN_IN ? "sign in" : "create account"), SPOT_SUBMIT},
                {std::format("Ctrl+Tab:  {}", mode == Mode::SIGN_IN ? "create an account" : "sign in instead"), SPOT_SWITCH},
                {"Esc:  play offline", SPOT_BACK},
            };
        }

        olc::FontRect footer_bounds = text_bounds(skin->display_font_small, "Hgjy");
        int32_t footer_bottom = screen.y - (int32_t)(screen.y * Ui::layout("account.footer_bottom"));
        footer_spots.clear();
        draw_hints(window, skin->display_font_small, footer, {(float)x, (float)(footer_bottom - (footer_bounds.offset.y + footer_bounds.size.y))}, Ui::layout_pxf("account.footer_hint_gap"), skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), footer_spots);
    }
}
