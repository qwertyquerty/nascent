#include "BeatmapDownloads.h"

#include "gse/Platform.h"
#include "ServerClient.h"
#include "SettingsManager.h"
#include "json.h"
#include "Utf8Path.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <cctype>
#include <cstdio>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace nascent {
    static std::mutex downloads_mutex;
    static std::map<std::string, std::shared_ptr<BeatmapDownloads::Download>> downloads;

    static std::string osu_key(int64_t set_id) {
        return std::format("osu:{}", set_id);
    }

    static std::string pack_key(int64_t pack_id) {
        return std::format("etterna:{}", pack_id);
    }
    static std::atomic<bool> library_changed = false;
    static std::atomic<bool> access_denied = false;

    static std::string folder_name(const std::string& text) {
        std::string name;

        for (unsigned char c : text) {
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

    static bool is_valid_utf8(const std::string& text) {
        for (size_t i = 0; i < text.size();) {
            unsigned char c = (unsigned char)text[i];
            size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2
                : (c >> 4) == 0xe                          ? 3
                : (c >> 3) == 0x1e                         ? 4
                                                           : 0;

            if (length == 0 || i + length > text.size()) {
                return false;
            }

            for (size_t j = 1; j < length; j++) {
                if (((unsigned char)text[i + j] & 0xc0) != 0x80) {
                    return false;
                }
            }

            i += length;
        }

        return true;
    }

    static std::string safe_component(std::string part) {
        for (char& c : part) {
            if ((unsigned char)c < 0x20 || c == 0x7f || std::string_view("<>:\"\\|?*").find(c) != std::string_view::npos) {
                c = '_';
            }
        }

        while (!part.empty() && (part.back() == ' ' || part.back() == '.')) {
            part.pop_back();
        }

        std::string stem = part.substr(0, part.find('.'));

        for (char& c : stem) {
            c = (char)std::toupper((unsigned char)c);
        }

        bool reserved = stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || ((stem.starts_with("COM") || stem.starts_with("LPT")) && stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9');
        return reserved ? "_" + part : part;
    }

    static std::optional<std::filesystem::path> safe_entry(std::string name) {
        if (!is_valid_utf8(name)) {
            for (char& c : name) {
                if ((unsigned char)c >= 0x80) {
                    c = '_';
                }
            }
        }

        std::filesystem::path entry;
        size_t start = 0;

        while (start <= name.size()) {
            size_t slash = name.find_first_of("/\\", start);
            std::string part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            start = slash == std::string::npos ? name.size() + 1 : slash + 1;

            if (part.empty() || part == ".") {
                continue;
            }

            if (part == "..") {
                return std::nullopt;
            }

            part = safe_component(part);

            if (part.empty()) {
                return std::nullopt;
            }

            entry /= utf8_path(part);
        }

        return entry.empty() ? std::nullopt : std::optional<std::filesystem::path>(entry);
    }

    static bool fits(const std::filesystem::path& target) {
        std::error_code ec;
        std::filesystem::path absolute = std::filesystem::absolute(target, ec);
        return target.filename().u8string().size() <= BeatmapDownloads::MAX_FILE_NAME_BYTES && (ec || absolute.native().size() <= BeatmapDownloads::MAX_PATH_LENGTH);
    }

    static std::filesystem::path shorten_chart_file(const std::filesystem::path& target, mz_uint index) {
        std::string stem = std::string(reinterpret_cast<const char*>(target.stem().u8string().c_str()));
        size_t open = stem.rfind('[');
        size_t close = stem.rfind(']');

        if (open != std::string::npos && close != std::string::npos && close > open) {
            std::filesystem::path difficulty = target.parent_path() / utf8_path(stem.substr(open, close - open + 1) + ".osu");

            if (fits(difficulty)) {
                return difficulty;
            }
        }

        return target.parent_path() / std::format("{}.osu", index);
    }

    static bool extract(const std::string& archive, const std::filesystem::path& destination, std::string& error) {
        mz_zip_archive zip = {};

        if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0)) {
            error = "it is not a valid .osz archive";
            return false;
        }

        size_t charts_written = 0;
        std::string first_failure;
        mz_uint count = mz_zip_reader_get_num_files(&zip);

        for (mz_uint index = 0; index < count; index++) {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(&zip, index, &stat) || mz_zip_reader_is_file_a_directory(&zip, index)) {
                continue;
            }

            std::optional<std::filesystem::path> entry = safe_entry(stat.m_filename);

            if (!entry) {
                continue;
            }

            std::filesystem::path target = destination / *entry;
            bool is_chart = target.extension() == ".osu";

            if (is_chart && !fits(target)) {
                target = shorten_chart_file(target, index);
            }

            std::string failure;
            size_t size = 0;
            void* data = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);

            if (data == nullptr) {
                failure = "it could not be decompressed";
            }
            else if (!fits(target)) {
                failure = "its path is too long for this system";
            }
            else {
                std::error_code ec;
                std::filesystem::create_directories(target.parent_path(), ec);
                errno = 0;
                std::ofstream file(target, std::ios::binary | std::ios::trunc);
                file.write(static_cast<const char*>(data), (std::streamsize)size);

                if (!file) {
                    if (ec == std::errc::permission_denied || errno == EACCES) {
                        access_denied = true;
                    }

                    failure = ec ? ec.message() : std::string("the file could not be created");
                }
            }

            mz_free(data);

            if (failure.empty()) {
                charts_written += is_chart;
                continue;
            }

            std::cerr << std::format("Skipped {} from the download: {}", stat.m_filename, failure) << std::endl;

            if (first_failure.empty() || is_chart) {
                first_failure = std::format("could not write {}: {}", stat.m_filename, failure);
            }
        }

        mz_zip_reader_end(&zip);

        if (charts_written == 0) {
            error = first_failure.empty() ? std::string("it has no .osu files") : first_failure;
            return false;
        }

        return true;
    }

    static bool install(const std::string& archive, const std::filesystem::path& destination, std::string& error) {
        std::error_code ec;

        if (std::filesystem::exists(destination, ec)) {
            return extract(archive, destination, error);
        }

        std::filesystem::path staging = destination;
        staging += ".part";
        std::filesystem::remove_all(staging, ec);

        if (!extract(archive, staging, error)) {
            std::filesystem::remove_all(staging, ec);
            return false;
        }

        if (!move_into_place(staging, destination, ec)) {
            if (ec == std::errc::permission_denied) {
                access_denied = true;
            }

            error = std::format("could not move the beatmap into the songs folder: {}", ec.message());
            std::filesystem::remove_all(staging, ec);
            return false;
        }

        return true;
    }

    static std::string host_of(const std::string& url) {
        size_t start = url.find("://");
        start = start == std::string::npos ? 0 : start + 3;
        return url.substr(start, url.find('/', start) - start);
    }

    static bool is_archive(const std::string& data) {
        mz_zip_archive zip = {};
        bool valid = mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0);

        if (valid) {
            mz_zip_reader_end(&zip);
        }

        return valid;
    }

    static std::optional<std::string> fetch_archive(const std::vector<std::string>& urls, BeatmapDownloads::Download& download, std::string& error) {
        for (const std::string& url : urls) {
            for (int32_t attempt = 1; attempt <= BeatmapDownloads::DOWNLOAD_ATTEMPTS; attempt++) {
                if (download.cancelled) {
                    return std::nullopt;
                }

                bool retryable = false;
                std::string attempt_error;
                download.progress = 0.0;
                std::optional<std::string> archive = ServerClient::download(url, BeatmapDownloads::MAX_DOWNLOAD_BYTES, [&download](double progress) { download.progress = progress; }, attempt_error, retryable, &download.cancelled);

                if (archive && is_archive(*archive)) {
                    return archive;
                }

                error = std::format("{}: {}", host_of(url), archive ? std::string("the download is not a valid .osz archive") : attempt_error);

                if (archive || !retryable) {
                    break;
                }

                if (attempt < BeatmapDownloads::DOWNLOAD_ATTEMPTS) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(BeatmapDownloads::RETRY_DELAY_MS));
                }
            }
        }

        return std::nullopt;
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
        auto existing = downloads.find(osu_key(set_id));

        if (existing != downloads.end() && existing->second->state != State::FAILED) {
            return existing->second;
        }

        auto download = std::make_shared<Download>();
        downloads[osu_key(set_id)] = download;

        std::vector<std::string> urls;

        for (std::string url : SettingsManager::settings.beatmap_download_urls) {
            size_t placeholder = url.find("{id}");

            if (placeholder != std::string::npos) {
                url.replace(placeholder, 4, std::to_string(set_id));
            }

            urls.push_back(std::move(url));
        }

        std::filesystem::path destination = SettingsManager::song_directory() / utf8_path(folder_name(std::format("{} {} - {}", set_id, artist, title)));
        download->destination = destination;

        std::thread([download, urls, destination, set_id] {
            std::string error = "no beatmap download mirrors are configured";
            std::optional<std::string> archive = fetch_archive(urls, *download, error);

            if (download->cancelled) {
                return;
            }

            if (!archive) {
                std::cerr << std::format("Beatmap {} failed to download: {}", set_id, error) << std::endl;
                download->fail(error);
                return;
            }

            download->state = State::EXTRACTING;

            if (!install(*archive, destination, error)) {
                std::cerr << std::format("Beatmap {} failed to install into {}: {}", set_id, path_utf8(destination), error) << std::endl;
                download->fail(error);
                return;
            }

            download->progress = 1.0;
            download->state = State::DONE;
            library_changed = true;
        }).detach();

        return download;
    }

    static std::shared_ptr<BeatmapDownloads::Download> find_key(const std::string& key) {
        std::lock_guard<std::mutex> lock(downloads_mutex);
        auto existing = downloads.find(key);
        return existing != downloads.end() ? existing->second : nullptr;
    }

    static bool cancel_key(const std::string& key) {
        std::lock_guard<std::mutex> lock(downloads_mutex);
        auto existing = downloads.find(key);

        if (existing == downloads.end() || existing->second->state != BeatmapDownloads::State::DOWNLOADING) {
            return false;
        }

        existing->second->cancelled = true;
        downloads.erase(existing);
        return true;
    }

    std::shared_ptr<BeatmapDownloads::Download> BeatmapDownloads::find(int64_t set_id) {
        return find_key(osu_key(set_id));
    }

    bool BeatmapDownloads::cancel(int64_t set_id) {
        return cancel_key(osu_key(set_id));
    }

    std::shared_ptr<BeatmapDownloads::Download> BeatmapDownloads::find_etterna_pack(int64_t pack_id) {
        return find_key(pack_key(pack_id));
    }

    bool BeatmapDownloads::cancel_etterna_pack(int64_t pack_id) {
        return cancel_key(pack_key(pack_id));
    }

    static std::string quote_url(const std::string& url) {
        static constexpr const char* HEX = "0123456789ABCDEF";
        std::string quoted;

        for (size_t index = 0; index < url.size(); index++) {
            unsigned char c = (unsigned char)url[index];
            bool escape = c == '%' && index + 2 < url.size() && std::isxdigit((unsigned char)url[index + 1]) && std::isxdigit((unsigned char)url[index + 2]);

            if (std::isalnum(c) || std::string_view("-._~:/").find((char)c) != std::string_view::npos || escape) {
                quoted.push_back((char)c);
            }
            else {
                quoted.push_back('%');
                quoted.push_back(HEX[c >> 4]);
                quoted.push_back(HEX[c & 0x0f]);
            }
        }

        return quoted;
    }

    BeatmapDownloads::PackLookup BeatmapDownloads::lookup_etterna_pack(int64_t pack_id) {
        PackLookup lookup;
        bool retryable = false;
        std::optional<std::string> body = ServerClient::download(std::format("{}{}", ETTERNA_PACK_API, pack_id), MAX_PACK_LOOKUP_BYTES, [](double) {}, lookup.error, retryable, nullptr, {"Accept: application/json"});

        if (!body) {
            lookup.error = std::format("could not reach EtternaOnline: {}", lookup.error);
            return lookup;
        }

        try {
            nlohmann::json data = nlohmann::json::parse(*body).at("data");
            EtternaPack pack;
            pack.id = pack_id;
            pack.name = data.value("name", std::string());
            pack.song_count = data.value("song_count", (int64_t)0);
            pack.size = data.value("size", std::string());
            pack.download_url = quote_url(data.at("download").get<std::string>());

            if (!pack.download_url.starts_with("https://")) {
                lookup.error = "EtternaOnline has no download for this pack";
                return lookup;
            }

            lookup.pack = std::move(pack);
        }
        catch (const nlohmann::json::exception&) {
            lookup.error = "EtternaOnline sent an unexpected answer";
        }

        return lookup;
    }

    static FILE* open_for_reading(const std::filesystem::path& path) {
#ifdef _WIN32
        return _wfopen(path.c_str(), L"rb");
#else
        return std::fopen(path.c_str(), "rb");
#endif
    }

    static size_t write_entry(void* target, mz_uint64, const void* data, size_t size) {
        std::ofstream* file = static_cast<std::ofstream*>(target);
        file->write(static_cast<const char*>(data), (std::streamsize)size);
        return *file ? size : 0;
    }

    static bool is_chart_name(const std::filesystem::path& path) {
        std::string extension = path_utf8(path.extension());

        for (char& c : extension) {
            c = (char)std::tolower((unsigned char)c);
        }

        return extension == ".sm" || extension == ".ssc" || extension == ".dwi" || extension == ".osu";
    }

    static bool extract_pack(const std::filesystem::path& archive, const std::filesystem::path& destination, std::atomic<bool>& cancelled, std::string& error) {
        std::error_code ec;
        uint64_t archive_size = std::filesystem::file_size(archive, ec);
        FILE* handle = ec ? nullptr : open_for_reading(archive);

        if (handle == nullptr) {
            error = "could not open the downloaded pack";
            return false;
        }

        mz_zip_archive zip = {};

        if (!mz_zip_reader_init_cfile(&zip, handle, archive_size, 0)) {
            std::fclose(handle);
            error = "the download is not a valid zip archive";
            return false;
        }

        mz_uint count = mz_zip_reader_get_num_files(&zip);
        std::optional<std::string> shared_root;
        bool rooted = true;

        for (mz_uint index = 0; index < count && rooted; index++) {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(&zip, index, &stat) || mz_zip_reader_is_file_a_directory(&zip, index)) {
                continue;
            }

            std::string name = stat.m_filename;
            size_t slash = name.find_first_of("/\\");

            if (slash == std::string::npos) {
                rooted = false;
                continue;
            }

            std::string root = name.substr(0, slash);
            rooted = !shared_root || *shared_root == root;
            shared_root = root;
        }

        size_t strip = rooted && shared_root ? shared_root->size() + 1 : 0;
        size_t charts_written = 0;
        std::string first_failure;

        for (mz_uint index = 0; index < count && !cancelled; index++) {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(&zip, index, &stat) || mz_zip_reader_is_file_a_directory(&zip, index)) {
                continue;
            }

            std::string name = stat.m_filename;
            std::optional<std::filesystem::path> entry = safe_entry(name.size() > strip ? name.substr(strip) : name);

            if (!entry) {
                continue;
            }

            std::filesystem::path target = destination / *entry;
            bool is_chart = is_chart_name(target);
            std::string failure;

            if (!fits(target)) {
                failure = "its path is too long for this system";
            }
            else {
                std::filesystem::create_directories(target.parent_path(), ec);
                errno = 0;
                std::ofstream file(target, std::ios::binary | std::ios::trunc);

                if (!file) {
                    if (ec == std::errc::permission_denied || errno == EACCES) {
                        access_denied = true;
                    }

                    failure = ec ? ec.message() : std::string("the file could not be created");
                }
                else if (!mz_zip_reader_extract_to_callback(&zip, index, write_entry, &file, 0)) {
                    failure = "it could not be decompressed";
                }
            }

            if (failure.empty()) {
                charts_written += is_chart;
                continue;
            }

            std::cerr << std::format("Skipped {} from the pack: {}", name, failure) << std::endl;

            if (first_failure.empty() || is_chart) {
                first_failure = std::format("could not write {}: {}", name, failure);
            }
        }

        mz_zip_reader_end(&zip);
        std::fclose(handle);

        if (cancelled) {
            error = "the download was cancelled";
            return false;
        }

        if (charts_written == 0) {
            error = first_failure.empty() ? std::string("it has no charts") : first_failure;
            return false;
        }

        return true;
    }

    static bool install_pack(const std::filesystem::path& archive, const std::filesystem::path& destination, std::atomic<bool>& cancelled, std::string& error) {
        std::error_code ec;

        if (std::filesystem::exists(destination, ec)) {
            return extract_pack(archive, destination, cancelled, error);
        }

        std::filesystem::path staging = destination;
        staging += ".part";
        std::filesystem::remove_all(staging, ec);

        if (!extract_pack(archive, staging, cancelled, error)) {
            std::filesystem::remove_all(staging, ec);
            return false;
        }

        if (!move_into_place(staging, destination, ec)) {
            if (ec == std::errc::permission_denied) {
                access_denied = true;
            }

            error = std::format("could not move the pack into the songs folder: {}", ec.message());
            std::filesystem::remove_all(staging, ec);
            return false;
        }

        return true;
    }

    bool BeatmapDownloads::archive_has_charts(const std::filesystem::path& file) {
        std::error_code ec;
        uint64_t archive_size = std::filesystem::file_size(file, ec);
        FILE* handle = ec ? nullptr : open_for_reading(file);

        if (handle == nullptr) {
            return false;
        }

        mz_zip_archive zip = {};
        bool found = false;

        if (mz_zip_reader_init_cfile(&zip, handle, archive_size, 0)) {
            mz_uint count = mz_zip_reader_get_num_files(&zip);

            for (mz_uint index = 0; index < count && !found; index++) {
                mz_zip_archive_file_stat stat;
                found = mz_zip_reader_file_stat(&zip, index, &stat) && !mz_zip_reader_is_file_a_directory(&zip, index) && is_chart_name(utf8_path(std::string(stat.m_filename)));
            }

            mz_zip_reader_end(&zip);
        }

        std::fclose(handle);
        return found;
    }

    BeatmapDownloads::ImportResult BeatmapDownloads::import_pack(const std::filesystem::path& file) {
        ImportResult result;
        result.name = folder_name(path_utf8(file.stem()));

        if (result.name.empty()) {
            result.name = "Imported pack";
        }

        std::atomic<bool> cancelled = false;

        if (!install_pack(file, SettingsManager::song_directory() / utf8_path(result.name), cancelled, result.error)) {
            return result;
        }

        library_changed = true;
        result.ok = true;
        return result;
    }

    std::shared_ptr<BeatmapDownloads::Download> BeatmapDownloads::start_etterna_pack(const EtternaPack& pack) {
        std::lock_guard<std::mutex> lock(downloads_mutex);
        std::string key = pack_key(pack.id);
        auto existing = downloads.find(key);

        if (existing != downloads.end() && existing->second->state != State::FAILED) {
            return existing->second;
        }

        auto download = std::make_shared<Download>();
        downloads[key] = download;
        std::string name = folder_name(pack.name);
        std::filesystem::path destination = SettingsManager::song_directory() / utf8_path(name.empty() ? std::format("EtternaOnline pack {}", pack.id) : name);
        std::filesystem::path archive = SettingsManager::song_directory() / std::format(".etterna-pack-{}.zip.part", pack.id);
        download->destination = destination;

        std::thread([download, pack, destination, archive] {
            std::string error;
            bool downloaded = false;
            std::error_code ec;

            for (int32_t attempt = 1; attempt <= DOWNLOAD_ATTEMPTS && !downloaded && !download->cancelled; attempt++) {
                bool retryable = false;
                download->progress = 0.0;
                downloaded = ServerClient::download_to_file(pack.download_url, archive, MAX_PACK_BYTES, [&download](double progress) { download->progress = progress; }, error, retryable, &download->cancelled);

                if (!downloaded && (!retryable || attempt == DOWNLOAD_ATTEMPTS)) {
                    break;
                }

                if (!downloaded) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_DELAY_MS));
                }
            }

            if (download->cancelled) {
                std::filesystem::remove(archive, ec);
                return;
            }

            if (!downloaded) {
                std::filesystem::remove(archive, ec);
                std::cerr << std::format("EtternaOnline pack {} failed to download: {}", pack.id, error) << std::endl;
                download->fail(std::format("{}: {}", host_of(pack.download_url), error));
                return;
            }

            download->state = State::EXTRACTING;
            bool installed = install_pack(archive, destination, download->cancelled, error);
            std::filesystem::remove(archive, ec);

            if (!installed) {
                std::cerr << std::format("EtternaOnline pack {} failed to install into {}: {}", pack.id, path_utf8(destination), error) << std::endl;
                download->fail(error);
                return;
            }

            download->progress = 1.0;
            download->state = State::DONE;
            library_changed = true;
        }).detach();

        return download;
    }

    void BeatmapDownloads::forget_missing() {
        std::lock_guard<std::mutex> lock(downloads_mutex);
        std::erase_if(downloads, [](const auto& entry) {
            std::error_code ec;
            return entry.second->state == State::DONE && !std::filesystem::exists(entry.second->destination, ec);
        });
    }

    bool BeatmapDownloads::take_library_changed() {
        return library_changed.exchange(false);
    }

    bool BeatmapDownloads::take_access_denied() {
        return access_denied.exchange(false);
    }

    BeatmapDownloads::ImportResult BeatmapDownloads::import_archive(const std::filesystem::path& file) {
        ImportResult result;
        result.name = folder_name(path_utf8(file.stem()));

        if (result.name.empty()) {
            result.name = "Imported beatmap";
        }

        std::error_code ec;
        uintmax_t size = std::filesystem::file_size(file, ec);

        if (ec) {
            result.error = std::format("could not read it: {}", ec.message());
            return result;
        }

        if (size > MAX_DOWNLOAD_BYTES) {
            result.error = "it is too large";
            return result;
        }

        std::ifstream stream(file, std::ios::binary);
        std::string archive((size_t)size, '\0');
        stream.read(archive.data(), (std::streamsize)archive.size());

        if (!stream) {
            result.error = "could not read it";
            return result;
        }

        if (!install(archive, SettingsManager::song_directory() / utf8_path(result.name), result.error)) {
            return result;
        }

        library_changed = true;
        result.ok = true;
        return result;
    }
}
