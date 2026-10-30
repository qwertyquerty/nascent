#pragma once

#include "olcPixelGameEngine.h"

#include <array>
#include <filesystem>
#include <string>

namespace nascent {
    class Input;

    float ui_px(float size);

    class ImGuiBackend {
        protected:
        olc::PixelGameEngine* window;
        std::string ini_path;
        std::array<bool, 3> mouse_held = {};
        std::array<bool, (size_t)olc::Key::ENUM_END> key_held = {};
        bool ready = false;

        public:
        explicit ImGuiBackend(olc::PixelGameEngine* window);
        ~ImGuiBackend();
        ImGuiBackend(const ImGuiBackend&) = delete;
        ImGuiBackend& operator=(const ImGuiBackend&) = delete;

        void init(const std::filesystem::path& settings_file, const std::filesystem::path& font_file, float font_size);
        void set_scale(float scale);
        void begin_frame(const Input& input, float elapsed_time);
        void end_frame();
        void render();
    };
}
