#pragma once

#include <atomic>
#include <cstdint>
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

            std::string error() const;
            void fail(std::string message);

            private:
            mutable std::mutex error_mutex;
            std::string error_message;
        };

        static constexpr size_t MAX_DOWNLOAD_BYTES = 512u * 1024u * 1024u;
        static constexpr size_t MAX_FOLDER_NAME_BYTES = 120;

        static std::shared_ptr<Download> start(int64_t set_id, const std::string& artist, const std::string& title);
        static std::shared_ptr<Download> find(int64_t set_id);
        static bool take_library_changed();
    };
}
