#include "FileDialog.h"
#include "ImGuiBackend.h"

#include "SettingsManager.h"
#include "Utf8Path.h"
#include "gse/Platform.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace nascent {
    namespace {
        std::string lowercase(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return text;
        }

        std::vector<std::filesystem::path> places() {
            std::vector<std::filesystem::path> result;
            std::error_code ec;

            if (const char* home = std::getenv("USERPROFILE"); home != nullptr) {
                result.emplace_back(home);
            }
            else if (const char* posix_home = std::getenv("HOME"); posix_home != nullptr) {
                result.emplace_back(posix_home);
            }

            if (!result.empty()) {
                for (const char* name : {"Desktop", "Downloads", "Music"}) {
                    if (std::filesystem::is_directory(result.front() / name, ec)) {
                        result.push_back(result.front() / name);
                    }
                }
            }

            std::filesystem::path songs = user_config_directory(SettingsManager::APP_NAME) / SettingsManager::SONG_DIRECTORY_NAME;

            if (std::filesystem::is_directory(songs, ec)) {
                result.push_back(songs);
            }

#ifdef _WIN32
            for (char drive = 'A'; drive <= 'Z'; drive++) {
                std::filesystem::path root = std::string(1, drive) + ":/";

                if (std::filesystem::exists(root, ec)) {
                    result.push_back(root);
                }
            }
#else
            result.emplace_back("/");
#endif

            return result;
        }
    }

    void FileDialog::open(Mode dialog_mode, std::string dialog_title, const std::filesystem::path& start, std::vector<std::string> wanted, std::string suggested_name) {
        std::error_code ec;
        mode = dialog_mode;
        title = std::move(dialog_title);
        id = title + "###file_dialog";
        extensions.clear();

        for (const std::string& extension : wanted) {
            extensions.push_back(lowercase(extension));
        }

        folder = std::filesystem::is_directory(start, ec) ? start : start.parent_path();

        if (folder.empty() || !std::filesystem::is_directory(folder, ec)) {
            folder = std::filesystem::current_path(ec);
        }

        file_name = std::move(suggested_name);
        error.clear();
        refresh();
        requested = true;
        active = true;
    }

    bool FileDialog::is_open() const {
        return active;
    }

    bool FileDialog::accepts(const std::filesystem::path& path) const {
        if (extensions.empty()) {
            return true;
        }

        std::string extension = lowercase(path_utf8(path.extension()));
        return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
    }

    void FileDialog::refresh() {
        entries.clear();
        folder_text = path_utf8(folder);
        std::error_code ec;

        for (std::filesystem::directory_iterator it(folder, std::filesystem::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code entry_ec;
            bool directory = it->is_directory(entry_ec);

            if (!directory && !accepts(it->path())) {
                continue;
            }

            std::string label = path_utf8(it->path().filename());

            if (!label.empty() && label.front() == '.') {
                continue;
            }

            entries.push_back({it->path(), std::move(label), directory});
        }

        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.directory != b.directory) {
                return a.directory;
            }

            return lowercase(a.label) < lowercase(b.label);
        });
    }

    std::optional<std::filesystem::path> FileDialog::finish() {
        if (file_name.empty()) {
            error = "Choose a file name";
            return std::nullopt;
        }

        std::filesystem::path chosen = folder / utf8_path(file_name);

        if (mode == Mode::SAVE && !extensions.empty() && !accepts(chosen)) {
            chosen += utf8_path(extensions.front());
        }

        std::error_code ec;

        if (mode == Mode::OPEN && !std::filesystem::is_regular_file(chosen, ec)) {
            error = "That file doesn't exist";
            return std::nullopt;
        }

        active = false;
        ImGui::CloseCurrentPopup();
        return chosen;
    }

    std::optional<std::filesystem::path> FileDialog::draw() {
        if (requested) {
            ImGui::OpenPopup(id.c_str());
            requested = false;
        }

        ImGui::SetNextWindowSize(ImVec2(ui_px(760), ui_px(520)), ImGuiCond_Appearing);
        std::optional<std::filesystem::path> result;
        bool keep_open = true;

        if (!ImGui::BeginPopupModal(id.c_str(), &keep_open)) {
            active = false;
            return std::nullopt;
        }

        if (!keep_open) {
            active = false;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return std::nullopt;
        }

        std::filesystem::path navigate;

        if (ImGui::Button("Up") && folder.has_parent_path() && folder.parent_path() != folder) {
            navigate = folder.parent_path();
        }

        ImGui::SameLine();

        if (ImGui::BeginCombo("##places", "Places", ImGuiComboFlags_WidthFitPreview)) {
            std::vector<std::filesystem::path> shortcuts = places();

            for (size_t index = 0; index < shortcuts.size(); index++) {
                ImGui::PushID((int)index);

                if (ImGui::Selectable(path_utf8(shortcuts[index]).c_str())) {
                    navigate = shortcuts[index];
                }

                ImGui::PopID();
            }

            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);

        if (ImGui::InputText("##folder", &folder_text, ImGuiInputTextFlags_EnterReturnsTrue)) {
            navigate = utf8_path(folder_text);
        }

        float footer = ImGui::GetFrameHeightWithSpacing() * (mode == Mode::SAVE ? 2.0f : 1.0f) + ImGui::GetStyle().ItemSpacing.y * 2 + (error.empty() ? 0.0f : ImGui::GetTextLineHeightWithSpacing());

        if (ImGui::BeginChild("##entries", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            for (size_t index = 0; index < entries.size(); index++) {
                const Entry& entry = entries[index];
                std::string label = (entry.directory ? "[dir] " : "") + entry.label;
                bool selected = !entry.directory && entry.label == file_name;
                ImGui::PushID((int)index);

                if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (entry.directory) {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            navigate = entry.path;
                        }
                    }
                    else {
                        file_name = entry.label;

                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                            result = finish();
                        }
                    }
                }

                ImGui::PopID();
            }
        }

        ImGui::EndChild();

        if (mode == Mode::SAVE) {
            ImGui::SetNextItemWidth(-1);

            if (ImGui::InputTextWithHint("##name", "File name", &file_name, ImGuiInputTextFlags_EnterReturnsTrue)) {
                result = finish();
            }
        }

        if (!error.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", error.c_str());
        }

        if (ImGui::Button(mode == Mode::SAVE ? "Save" : "Open", ImVec2(ui_px(120), 0))) {
            result = finish();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(ui_px(120), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            active = false;
            ImGui::CloseCurrentPopup();
        }

        std::error_code ec;

        if (!navigate.empty() && std::filesystem::is_directory(navigate, ec)) {
            folder = navigate;
            error.clear();
            refresh();
        }

        ImGui::EndPopup();
        return result;
    }
}
