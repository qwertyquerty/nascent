#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
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
            std::filesystem::path destination;
            std::string name;
            bool pack = false;

            std::string error() const;
            void fail(std::string message);

            private:
            mutable std::mutex error_mutex;
            std::string error_message;
        };

        struct Activity {
            size_t songs = 0;
            size_t packs = 0;
            double progress = 0;
            bool installing = false;
            std::string name;
        };

        struct ImportResult {
            bool ok = false;
            std::string name;
            std::string error;
        };

        struct EtternaPack {
            int64_t id = 0;
            std::string name;
            int64_t song_count = 0;
            std::string size;
            std::string download_url;
        };

        struct PackLookup {
            std::optional<EtternaPack> pack;
            std::string error;
        };

        static constexpr size_t MAX_DOWNLOAD_BYTES = 512u * 1024u * 1024u;
        static constexpr size_t MAX_FOLDER_NAME_BYTES = 64;
        static constexpr size_t MAX_FILE_NAME_BYTES = 255;
        static constexpr size_t MAX_PATH_LENGTH = 259;
        static constexpr int32_t DOWNLOAD_ATTEMPTS = 2;
        static constexpr int32_t RETRY_DELAY_MS = 2000;
        static constexpr uint64_t MAX_PACK_BYTES = 16ull * 1024u * 1024u * 1024u;
        static constexpr size_t MAX_PACK_LOOKUP_BYTES = 4u * 1024u * 1024u;
        static constexpr const char* ETTERNA_PACK_API = "https://api.etternaonline.com/api/packs/";

        static std::shared_ptr<Download> start(int64_t set_id, const std::string& artist, const std::string& title);
        static std::shared_ptr<Download> find(int64_t set_id);
        static bool cancel(int64_t set_id);
        static PackLookup lookup_etterna_pack(int64_t pack_id);
        static std::shared_ptr<Download> start_etterna_pack(const EtternaPack& pack);
        static std::shared_ptr<Download> find_etterna_pack(int64_t pack_id);
        static bool cancel_etterna_pack(int64_t pack_id);
        static bool take_library_changed();
        static std::optional<Activity> activity();
        static void forget_missing();
        static bool take_access_denied();
        static ImportResult import_archive(const std::filesystem::path& file);
        static bool archive_has_charts(const std::filesystem::path& file);
        static ImportResult import_pack(const std::filesystem::path& file);
    };
}
