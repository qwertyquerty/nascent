#include "BeatmapDownloads.h"

#include "ServerClient.h"
#include "SettingsManager.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <thread>
#include <utility>

namespace nascent {
    static std::mutex downloads_mutex;
    static std::map<int64_t, std::shared_ptr<BeatmapDownloads::Download>> downloads;
    static std::atomic<bool> library_changed = false;

    static std::string folder_name(int64_t set_id, const std::string& artist, const std::string& title) {
        std::string name;

        for (unsigned char c : std::format("{} {} - {}", set_id, artist, title)) {
            if (c >= 0x20 && c != 0x7f && std::string_view("<>:\"/\\|?*").find((char)c) == std::string_view::npos) {
                name.push_back((char)c);
            }
        }

        if (name.size() > BeatmapDownloads::MAX_FOLDER_NAME_BYTES) {
            name.resize(BeatmapDownloads::MAX_FOLDER_NAME_BYTES);

            while (!name.empty() && ((unsigned char)name.back() & 0xc0) == 0x80) {
                name.pop_back();
            }

            if (!name.empty() && (unsigned char)name.back() >= 0xc0) {
                name.pop_back();
            }
        }

        while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
            name.pop_back();
        }

        return name;
    }

    static std::filesystem::path utf8_path(const std::string& text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    static bool is_safe_entry(const std::filesystem::path& entry) {
        if (entry.empty() || entry.has_root_name() || entry.has_root_directory()) {
            return false;
        }

        for (const std::filesystem::path& part : entry) {
            if (part == "..") {
                return false;
            }
        }

        return true;
    }

    static bool extract(const std::string& archive, const std::filesystem::path& destination, std::string& error) {
        mz_zip_archive zip = {};

        if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0)) {
            error = "the download is not a valid .osz archive";
            return false;
        }

        bool ok = true;
        mz_uint count = mz_zip_reader_get_num_files(&zip);

        for (mz_uint index = 0; index < count && ok; index++) {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(&zip, index, &stat) || mz_zip_reader_is_file_a_directory(&zip, index)) {
                continue;
            }

            std::filesystem::path entry = utf8_path(stat.m_filename).lexically_normal();

            if (!is_safe_entry(entry)) {
                continue;
            }

            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);

            if (data == nullptr) {
                error = std::format("could not extract {}", stat.m_filename);
                ok = false;
                break;
            }

            std::filesystem::path target = destination / entry;
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            std::ofstream file(target, std::ios::binary | std::ios::trunc);
            file.write(static_cast<const char*>(data), (std::streamsize)size);
            mz_free(data);

            if (!file) {
                error = std::format("could not write {}", stat.m_filename);
                ok = false;
            }
        }

        mz_zip_reader_end(&zip);
        return ok;
    }

    std::string BeatmapDownloads::Download::error() const {
        std::lock_guard<std::mutex> lock(error_mutex);
        return error_message;
    }

    void BeatmapDownloads::Download::fail(std::string message) {
        {
            std::lock_guard<std::mutex> lock(error_mutex);
            error_message = std::move(message);
        }

        state = State::FAILED;
    }

    std::shared_ptr<BeatmapDownloads::Download> BeatmapDownloads::start(int64_t set_id, const std::string& artist, const std::string& title) {
        std::lock_guard<std::mutex> lock(downloads_mutex);
        auto existing = downloads.find(set_id);

        if (existing != downloads.end() && existing->second->state != State::FAILED) {
            return existing->second;
        }

        auto download = std::make_shared<Download>();
        downloads[set_id] = download;

        std::string url = SettingsManager::settings.beatmap_download_url;
        size_t placeholder = url.find("{id}");

        if (placeholder != std::string::npos) {
            url.replace(placeholder, 4, std::to_string(set_id));
        }

        std::filesystem::path destination = std::filesystem::path(SettingsManager::settings.list_song_directory) / utf8_path(folder_name(set_id, artist, title));

        std::thread([download, url, destination] {
            std::string error;
            std::optional<std::string> archive = ServerClient::download(url, MAX_DOWNLOAD_BYTES, [&download](double progress) { download->progress = progress; }, error);

            if (!archive) {
                download->fail(error);
                return;
            }

            download->state = State::EXTRACTING;
            std::error_code ec;

            if (std::filesystem::exists(destination, ec)) {
                if (!extract(*archive, destination, error)) {
                    download->fail(error);
                    return;
                }
            }
            else {
                std::filesystem::path staging = destination;
                staging += ".part";
                std::filesystem::remove_all(staging, ec);

                if (!extract(*archive, staging, error)) {
                    std::filesystem::remove_all(staging, ec);
                    download->fail(error);
                    return;
                }

                std::filesystem::rename(staging, destination, ec);

                if (ec) {
                    std::filesystem::remove_all(staging, ec);
                    download->fail(std::format("could not move the beatmap into the songs folder: {}", ec.message()));
                    return;
                }
            }

            download->progress = 1.0;
            download->state = State::DONE;
            library_changed = true;
        }).detach();

        return download;
    }

    std::shared_ptr<BeatmapDownloads::Download> BeatmapDownloads::find(int64_t set_id) {
        std::lock_guard<std::mutex> lock(downloads_mutex);
        auto existing = downloads.find(set_id);
        return existing != downloads.end() ? existing->second : nullptr;
    }

    bool BeatmapDownloads::take_library_changed() {
        return library_changed.exchange(false);
    }
}
