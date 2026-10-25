#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class FileDialog {
        public:
        enum class Mode {
            OPEN,
            SAVE,
        };

        protected:
        struct Entry {
            std::filesystem::path path;
            std::string label;
            bool directory = false;
        };

        Mode mode = Mode::OPEN;
        std::string title;
        std::string id;
        std::filesystem::path folder;
        std::vector<std::string> extensions;
        std::vector<Entry> entries;
        std::string folder_text;
        std::string file_name;
        std::string error;
        bool requested = false;
        bool active = false;

        void refresh();
        bool accepts(const std::filesystem::path& path) const;
        std::optional<std::filesystem::path> finish();

        public:
        void open(Mode mode, std::string title, const std::filesystem::path& start, std::vector<std::string> extensions, std::string suggested_name = {});
        bool is_open() const;
        std::optional<std::filesystem::path> draw();
    };
}
