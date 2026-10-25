#include "ImGuiBackend.h"

#include "gse/Platform.h"
#include "gse/input/Input.h"

#include "imgui.h"
#include "imgui_impl_opengl2.h"

#include <algorithm>
#include <utility>

namespace nascent {
    namespace {
        struct KeyMapping {
            olc::Key key;
            ImGuiKey imgui;
        };

        constexpr KeyMapping KEY_MAPPINGS[] = {
            {olc::Key::A, ImGuiKey_A},
            {olc::Key::B, ImGuiKey_B},
            {olc::Key::C, ImGuiKey_C},
            {olc::Key::D, ImGuiKey_D},
            {olc::Key::E, ImGuiKey_E},
            {olc::Key::F, ImGuiKey_F},
            {olc::Key::G, ImGuiKey_G},
            {olc::Key::H, ImGuiKey_H},
            {olc::Key::I, ImGuiKey_I},
            {olc::Key::J, ImGuiKey_J},
            {olc::Key::K, ImGuiKey_K},
            {olc::Key::L, ImGuiKey_L},
            {olc::Key::M, ImGuiKey_M},
            {olc::Key::N, ImGuiKey_N},
            {olc::Key::O, ImGuiKey_O},
            {olc::Key::P, ImGuiKey_P},
            {olc::Key::Q, ImGuiKey_Q},
            {olc::Key::R, ImGuiKey_R},
            {olc::Key::S, ImGuiKey_S},
            {olc::Key::T, ImGuiKey_T},
            {olc::Key::U, ImGuiKey_U},
            {olc::Key::V, ImGuiKey_V},
            {olc::Key::W, ImGuiKey_W},
            {olc::Key::X, ImGuiKey_X},
            {olc::Key::Y, ImGuiKey_Y},
            {olc::Key::Z, ImGuiKey_Z},
            {olc::Key::K0, ImGuiKey_0},
            {olc::Key::K1, ImGuiKey_1},
            {olc::Key::K2, ImGuiKey_2},
            {olc::Key::K3, ImGuiKey_3},
            {olc::Key::K4, ImGuiKey_4},
            {olc::Key::K5, ImGuiKey_5},
            {olc::Key::K6, ImGuiKey_6},
            {olc::Key::K7, ImGuiKey_7},
            {olc::Key::K8, ImGuiKey_8},
            {olc::Key::K9, ImGuiKey_9},
            {olc::Key::F1, ImGuiKey_F1},
            {olc::Key::F2, ImGuiKey_F2},
            {olc::Key::F3, ImGuiKey_F3},
            {olc::Key::F4, ImGuiKey_F4},
            {olc::Key::F5, ImGuiKey_F5},
            {olc::Key::F6, ImGuiKey_F6},
            {olc::Key::F7, ImGuiKey_F7},
            {olc::Key::F8, ImGuiKey_F8},
            {olc::Key::F9, ImGuiKey_F9},
            {olc::Key::F10, ImGuiKey_F10},
            {olc::Key::F11, ImGuiKey_F11},
            {olc::Key::F12, ImGuiKey_F12},
            {olc::Key::UP, ImGuiKey_UpArrow},
            {olc::Key::DOWN, ImGuiKey_DownArrow},
            {olc::Key::LEFT, ImGuiKey_LeftArrow},
            {olc::Key::RIGHT, ImGuiKey_RightArrow},
            {olc::Key::SPACE, ImGuiKey_Space},
            {olc::Key::TAB, ImGuiKey_Tab},
            {olc::Key::SHIFT, ImGuiKey_LeftShift},
            {olc::Key::CTRL, ImGuiKey_LeftCtrl},
            {olc::Key::INS, ImGuiKey_Insert},
            {olc::Key::DEL, ImGuiKey_Delete},
            {olc::Key::HOME, ImGuiKey_Home},
            {olc::Key::END, ImGuiKey_End},
            {olc::Key::PGUP, ImGuiKey_PageUp},
            {olc::Key::PGDN, ImGuiKey_PageDown},
            {olc::Key::BACK, ImGuiKey_Backspace},
            {olc::Key::ESCAPE, ImGuiKey_Escape},
            {olc::Key::RETURN, ImGuiKey_Enter},
            {olc::Key::ENTER, ImGuiKey_KeypadEnter},
            {olc::Key::PAUSE, ImGuiKey_Pause},
            {olc::Key::SCROLL, ImGuiKey_ScrollLock},
            {olc::Key::NP0, ImGuiKey_Keypad0},
            {olc::Key::NP1, ImGuiKey_Keypad1},
            {olc::Key::NP2, ImGuiKey_Keypad2},
            {olc::Key::NP3, ImGuiKey_Keypad3},
            {olc::Key::NP4, ImGuiKey_Keypad4},
            {olc::Key::NP5, ImGuiKey_Keypad5},
            {olc::Key::NP6, ImGuiKey_Keypad6},
            {olc::Key::NP7, ImGuiKey_Keypad7},
            {olc::Key::NP8, ImGuiKey_Keypad8},
            {olc::Key::NP9, ImGuiKey_Keypad9},
            {olc::Key::NP_MUL, ImGuiKey_KeypadMultiply},
            {olc::Key::NP_DIV, ImGuiKey_KeypadDivide},
            {olc::Key::NP_ADD, ImGuiKey_KeypadAdd},
            {olc::Key::NP_SUB, ImGuiKey_KeypadSubtract},
            {olc::Key::NP_DECIMAL, ImGuiKey_KeypadDecimal},
            {olc::Key::PERIOD, ImGuiKey_Period},
            {olc::Key::EQUALS, ImGuiKey_Equal},
            {olc::Key::COMMA, ImGuiKey_Comma},
            {olc::Key::MINUS, ImGuiKey_Minus},
            {olc::Key::OEM_1, ImGuiKey_Semicolon},
            {olc::Key::OEM_2, ImGuiKey_Slash},
            {olc::Key::OEM_3, ImGuiKey_GraveAccent},
            {olc::Key::OEM_4, ImGuiKey_LeftBracket},
            {olc::Key::OEM_5, ImGuiKey_Backslash},
            {olc::Key::OEM_6, ImGuiKey_RightBracket},
            {olc::Key::OEM_7, ImGuiKey_Apostrophe},
        };
    }

    ImGuiBackend::ImGuiBackend(olc::PixelGameEngine* window) : window(window) {}

    ImGuiBackend::~ImGuiBackend() {
        if (!ready) {
            return;
        }

        ImGui_ImplOpenGL2_Shutdown();
        ImGui::DestroyContext();
    }

    void ImGuiBackend::init(const std::filesystem::path& settings_file, const std::filesystem::path& font_file, float font_size) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.BackendPlatformName = "nascent_pge";
        ini_path = settings_file.string();
        io.IniFilename = ini_path.c_str();

        std::error_code ec;

        if (std::filesystem::is_regular_file(font_file, ec)) {
            io.Fonts->AddFontFromFileTTF(font_file.string().c_str(), font_size);
        }
        else {
            io.Fonts->AddFontDefault();
        }

        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;

        ImGui_ImplOpenGL2_Init();
        ready = true;
    }

    void ImGuiBackend::begin_frame(const Input& input, float elapsed_time) {
        ImGuiIO& io = ImGui::GetIO();
        olc::vi2d screen = window->GetScreenSize();
        io.DisplaySize = ImVec2((float)screen.x, (float)screen.y);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::max(elapsed_time, 1e-5f);

        olc::vi2d mouse = input.mouse_position();
        io.AddMousePosEvent((float)mouse.x, (float)mouse.y);

        for (int button = 0; button < (int)mouse_held.size(); button++) {
            bool held = input.mouse((uint32_t)button).bHeld;

            if (held != mouse_held[(size_t)button]) {
                mouse_held[(size_t)button] = held;
                io.AddMouseButtonEvent(button, held);
            }
        }

        if (int32_t wheel = input.mouse_wheel(); wheel != 0) {
            io.AddMouseWheelEvent(0.0f, (float)wheel / 120.0f);
        }

        bool ctrl = input.key(olc::Key::CTRL).bHeld;
        bool shift = input.key(olc::Key::SHIFT).bHeld;
        bool alt = alt_held();
        io.AddKeyEvent(ImGuiMod_Ctrl, ctrl);
        io.AddKeyEvent(ImGuiMod_Shift, shift);
        io.AddKeyEvent(ImGuiMod_Alt, alt);

        for (const KeyMapping& mapping : KEY_MAPPINGS) {
            bool held = input.key(mapping.key).bHeld;
            bool& previous = key_held[(size_t)mapping.key];

            if (held != previous) {
                previous = held;
                io.AddKeyEvent(mapping.imgui, held);
            }
        }

        if (const std::string& text = input.text(); !text.empty()) {
            io.AddInputCharactersUTF8(text.c_str());
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui::NewFrame();
    }

    void ImGuiBackend::end_frame() {
        ImGui::Render();
    }

    void ImGuiBackend::render() {
        if (ImDrawData* data = ImGui::GetDrawData(); data != nullptr && data->Valid) {
            ImGui_ImplOpenGL2_RenderDrawData(data);
        }
    }
}
