#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>

namespace nascent {
    class BeatmapDownloads {
        public:
        enum class State {
            DOWNLOADING,
            EXTRACTING,
            DONE,
            FAILED
        };

        struct Download {
            std::atomic<State> state = State::DOWNLOADING;
            std::atomic<double> progress = 0.0;
            std::atomic<bool> cancelled = false;

            std::string error() const;
            void fail(std::string message);

            private:
            mutable std::mutex error_mutex;
            std::string error_message;
        };

        struct ImportResult {
            bool ok = false;
            std::string name;
            std::string error;
        };

        static constexpr size_t MAX_DOWNLOAD_BYTES = 512u * 1024u * 1024u;
        static constexpr size_t MAX_FOLDER_NAME_BYTES = 64;
        static constexpr size_t MAX_FILE_NAME_BYTES = 255;
        static constexpr size_t MAX_PATH_LENGTH = 259;
        static constexpr int32_t DOWNLOAD_ATTEMPTS = 2;
        static constexpr int32_t RETRY_DELAY_MS = 2000;

        static std::shared_ptr<Download> start(int64_t set_id, const std::string& artist, const std::string& title);
        static std::shared_ptr<Download> find(int64_t set_id);
        static bool cancel(int64_t set_id);
        static bool take_library_changed();
        static bool take_access_denied();
        static ImportResult import_archive(const std::filesystem::path& file);
    };
}
