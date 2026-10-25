#include "Updater.h"

#include "gse/Platform.h"
#include "ServerClient.h"
#include "SettingsManager.h"
#include "Sha256.h"
#include "Utf8Path.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace nascent {
    namespace {
#if defined(_WIN32)
        constexpr const char* PACKAGE_OS = "win";
        constexpr const char* EXECUTABLE_NAME = "Nascent.exe";
        constexpr const char* EDITOR_EXECUTABLE_NAME = "NascentEditor.exe";
#elif defined(__APPLE__)
        constexpr const char* PACKAGE_OS = "mac";
        constexpr const char* EXECUTABLE_NAME = "Nascent";
        constexpr const char* EDITOR_EXECUTABLE_NAME = "NascentEditor";
#else
        constexpr const char* PACKAGE_OS = "linux";
        constexpr const char* EXECUTABLE_NAME = "Nascent";
        constexpr const char* EDITOR_EXECUTABLE_NAME = "NascentEditor";
#endif

#if defined(__x86_64__) || defined(_M_X64)
        constexpr const char* PACKAGE_ARCH = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
        constexpr const char* PACKAGE_ARCH = "arm64";
#else
        constexpr const char* PACKAGE_ARCH = "unknown";
#endif

        constexpr const char* BUNDLE_NAME = "Nascent.app";
        constexpr const char* EDITOR_BUNDLE_NAME = "Nascent Editor.app";
        constexpr const char* EDITOR_APPIMAGE_PRODUCT = "nascent-editor";
        constexpr const char* STAGING_NAME = ".nascent-update";
        constexpr const char* STAGED_APPIMAGE = "new.AppImage";
        constexpr int MOVE_ATTEMPTS = 10;
        constexpr double MOVE_RETRY_SECONDS = 0.2;

        enum class InstallKind {
            FOLDER,
            BUNDLE,
            APPIMAGE,
        };

        struct InstallTarget {
            InstallKind kind = InstallKind::FOLDER;
            std::filesystem::path root;
            std::filesystem::path staging;
            UpdatePackage package = UpdatePackage::ZIP;
        };

        struct Move {
            std::filesystem::path from;
            std::filesystem::path to;
        };

        std::mutex mutex;
        UpdateSnapshot state;
        std::optional<UpdateNotice> notice;

        bool editor_running() {
            return executable_path().filename() == EDITOR_EXECUTABLE_NAME;
        }

        std::optional<InstallTarget> install_target() {
            std::filesystem::path executable = executable_path();
            std::error_code ec;

            if (executable.empty()) {
                return std::nullopt;
            }

#if defined(__linux__)
            if (const char* appimage = std::getenv("APPIMAGE"); appimage != nullptr && *appimage != '\0') {
                std::filesystem::path file = std::filesystem::absolute(appimage, ec);

                if (ec || !std::filesystem::is_regular_file(file, ec)) {
                    return std::nullopt;
                }

                return InstallTarget{InstallKind::APPIMAGE, file, file.parent_path() / STAGING_NAME, UpdatePackage::APPIMAGE};
            }
#endif

            std::filesystem::path directory = executable.parent_path();

#if defined(__APPLE__)
            std::filesystem::path bundle = directory.parent_path().parent_path();

            if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents" && bundle.extension() == ".app") {
                return InstallTarget{InstallKind::BUNDLE, bundle, bundle.parent_path() / STAGING_NAME, UpdatePackage::ZIP};
            }

            return std::nullopt;
#else
            if (!std::filesystem::is_directory(directory / "assets", ec)) {
                return std::nullopt;
            }

            return InstallTarget{InstallKind::FOLDER, directory, directory / STAGING_NAME, UpdatePackage::ZIP};
#endif
        }

        bool writable(const InstallTarget& target) {
            std::error_code ec;
            bool existed = std::filesystem::exists(target.staging, ec);
            std::filesystem::create_directories(target.staging, ec);
            std::filesystem::path probe = target.staging / "probe";
            bool created = (bool)std::ofstream(probe, std::ios::binary | std::ios::trunc);
            std::filesystem::remove(probe, ec);

            if (!existed) {
                std::filesystem::remove_all(target.staging, ec);
            }

            return created;
        }

        std::filesystem::path result_path() {
            return user_config_directory(SettingsManager::APP_NAME) / Updater::RESULT_FILE_NAME;
        }

        void write_result(const std::string& version, const std::string& error) {
            std::ofstream file(result_path(), std::ios::binary | std::ios::trunc);
            file << nlohmann::json{{"version", version}, {"error", error}}.dump();
        }

        void make_executable(const std::filesystem::path& path) {
#if !defined(_WIN32)
            std::error_code ec;
            std::filesystem::permissions(path, std::filesystem::perms::owner_all | std::filesystem::perms::group_read | std::filesystem::perms::group_exec | std::filesystem::perms::others_read | std::filesystem::perms::others_exec, ec);
#else
            (void)path;
#endif
        }

        bool write_file(const std::filesystem::path& path, const void* data, size_t size) {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file.write(static_cast<const char*>(data), (std::streamsize)size);
            return (bool)file;
        }

        std::optional<std::filesystem::path> package_entry(std::string_view name) {
            std::filesystem::path entry;

            while (!name.empty()) {
                size_t slash = name.find('/');
                std::string_view part = name.substr(0, slash);
                name = slash == std::string_view::npos ? std::string_view() : name.substr(slash + 1);

                if (part.empty() || part == "." || part == ".." || part.find_first_of("\\:") != std::string_view::npos) {
                    return std::nullopt;
                }

                entry /= utf8_path(part);
            }

            return entry;
        }

        bool extract_package(const std::string& package, const std::string& tag, const InstallTarget& target, std::string& error) {
            std::string prefix = std::format("nascent-{}-{}-{}/", tag, PACKAGE_OS, PACKAGE_ARCH);
            std::filesystem::path destination = target.staging / "new";
            mz_zip_archive zip = {};

            if (!mz_zip_reader_init_mem(&zip, package.data(), package.size(), 0)) {
                error = "the download is not a valid zip";
                return false;
            }

            mz_uint count = mz_zip_reader_get_num_files(&zip);

            for (mz_uint index = 0; index < count && error.empty(); index++) {
                mz_zip_archive_file_stat stat;

                if (!mz_zip_reader_file_stat(&zip, index, &stat)) {
                    error = "the download could not be read";
                    break;
                }

                std::string_view name = stat.m_filename;

                if (!name.starts_with(prefix)) {
                    error = std::format("the download has an unexpected file {}", name);
                    break;
                }

                name.remove_prefix(prefix.size());

                if (name.empty()) {
                    continue;
                }

                bool directory = name.ends_with('/') || mz_zip_reader_is_file_a_directory(&zip, index);
                std::optional<std::filesystem::path> entry = package_entry(directory && name.ends_with('/') ? name.substr(0, name.size() - 1) : name);

                if (!entry) {
                    error = std::format("the download has an unsafe file name {}", stat.m_filename);
                    break;
                }

                std::filesystem::path output = destination / *entry;

                if (directory) {
                    std::error_code ec;
                    std::filesystem::create_directories(output, ec);
                    continue;
                }

                size_t size = 0;
                void* data = mz_zip_reader_extract_to_heap(&zip, index, &size, 0);

                if (data == nullptr) {
                    error = std::format("{} could not be decompressed", name);
                    break;
                }

                if (!write_file(output, data, size)) {
                    error = std::format("{} could not be written", name);
                }

                mz_free(data);

                if (((stat.m_external_attr >> 16) & 0111) != 0) {
                    make_executable(output);
                }
            }

            mz_zip_reader_end(&zip);

            if (!error.empty()) {
                return false;
            }

            std::filesystem::path executable = target.kind == InstallKind::BUNDLE ? destination / BUNDLE_NAME / "Contents" / "MacOS" / "Nascent" : destination / EXECUTABLE_NAME;
            std::error_code ec;

            if (!std::filesystem::is_regular_file(executable, ec) || (target.kind == InstallKind::FOLDER && !std::filesystem::is_directory(destination / "assets", ec))) {
                error = "the download is missing the game";
                return false;
            }

            make_executable(executable);
            return true;
        }

        void set_progress(double fraction) {
            std::lock_guard<std::mutex> lock(mutex);
            state.progress = std::clamp(fraction, 0.0, 1.0);
        }

        bool stage(const UpdateRelease& release, const InstallTarget& target, std::string& error) {
            std::error_code ec;
            std::filesystem::remove_all(target.staging, ec);
            std::filesystem::create_directories(target.staging, ec);

            if (ec || !std::filesystem::is_directory(target.staging, ec)) {
                error = std::format("the game folder {} can't be written to", path_utf8(target.staging.parent_path()));
                return false;
            }

            bool retryable = false;
            std::optional<std::string> checksums = ServerClient::download(release.checksums.url, Updater::MAX_CHECKSUMS_BYTES, {}, error, retryable);

            if (!checksums) {
                return false;
            }

            std::optional<std::string> expected = checksum_for(*checksums, release.package.name);

            if (!expected) {
                error = "the release has no checksum for this download";
                return false;
            }

            size_t limit = release.package.size > 0 ? (size_t)std::min<uint64_t>(release.package.size, Updater::MAX_PACKAGE_BYTES) : Updater::MAX_PACKAGE_BYTES;
            std::function<void(double)> progress = set_progress;
            std::optional<std::string> package = ServerClient::download(release.package.url, limit, progress, error, retryable);

            if (!package) {
                return false;
            }

            if (sha256_hex(*package) != *expected) {
                error = "the download was corrupted";
                return false;
            }

            if (target.kind != InstallKind::APPIMAGE) {
                return extract_package(*package, release.tag, target, error);
            }

            std::filesystem::path staged = target.staging / STAGED_APPIMAGE;

            if (!write_file(staged, package->data(), package->size())) {
                error = "the update could not be saved";
                return false;
            }

            make_executable(staged);
            return true;
        }

        bool apply_moves(const std::vector<Move>& moves, std::string& error) {
            std::vector<const Move*> done;

            for (const Move& move : moves) {
                std::error_code ec;

                for (int attempt = 0; attempt < MOVE_ATTEMPTS; attempt++) {
                    ec.clear();
                    std::filesystem::rename(move.from, move.to, ec);

                    if (!ec) {
                        break;
                    }

                    precise_sleep(MOVE_RETRY_SECONDS);
                }

                if (ec) {
                    error = std::format("could not replace {}: {}", path_utf8(move.from.filename()), ec.message());

                    for (auto undo = done.rbegin(); undo != done.rend(); undo++) {
                        std::error_code ignored;
                        std::filesystem::rename((*undo)->to, (*undo)->from, ignored);
                    }

                    return false;
                }

                done.push_back(&move);
            }

            return true;
        }

        bool install(const InstallTarget& target, std::string& error) {
            std::error_code ec;
            std::filesystem::path incoming = target.staging / "new";
            std::filesystem::path outgoing = target.staging / "old";
            std::vector<Move> moves;

            if (target.kind == InstallKind::APPIMAGE) {
                return apply_moves({{target.staging / STAGED_APPIMAGE, target.root}}, error);
            }

            std::filesystem::create_directories(outgoing, ec);

            bool editor = editor_running();

            if (target.kind == InstallKind::BUNDLE) {
                for (const char* bundle : {BUNDLE_NAME, EDITOR_BUNDLE_NAME}) {
                    bool running_bundle = (bundle == EDITOR_BUNDLE_NAME) == editor;
                    std::filesystem::path current = running_bundle ? target.root : target.root.parent_path() / bundle;

                    if (!std::filesystem::exists(incoming / bundle, ec) || (!running_bundle && !std::filesystem::exists(current, ec))) {
                        continue;
                    }

                    moves.push_back({current, outgoing / bundle});
                    moves.push_back({incoming / bundle, current});
                }

                return apply_moves(moves, error);
            }

            std::filesystem::path running = executable_path().filename();

            for (const auto& entry : std::filesystem::directory_iterator(incoming, ec)) {
                std::filesystem::path name = entry.path().filename();

                if (name == EXECUTABLE_NAME && !running.empty() && !editor) {
                    name = running;
                }

                std::filesystem::path current = target.root / name;
                bool optional = editor ? name == EXECUTABLE_NAME : name == EDITOR_EXECUTABLE_NAME;

                if (optional && !std::filesystem::exists(current, ec)) {
                    continue;
                }

                if (std::filesystem::exists(current, ec)) {
                    moves.push_back({current, outgoing / name});
                }

                moves.push_back({entry.path(), current});
            }

            if (ec || moves.empty()) {
                error = "the update files could not be found";
                return false;
            }

            return apply_moves(moves, error);
        }

        bool relaunch(const InstallTarget& target) {
            switch (target.kind) {
                case InstallKind::APPIMAGE:
                    return spawn_detached(target.root, {});
                case InstallKind::BUNDLE:
                    return spawn_detached("/usr/bin/open", {"-n", target.root.string()});
                case InstallKind::FOLDER:
                    return spawn_detached(target.root / executable_path().filename(), {});
            }

            return false;
        }
    }

    void Updater::recover() {
        std::error_code ec;

        if (std::optional<InstallTarget> target = install_target()) {
            std::filesystem::remove_all(target->staging, ec);
        }

        std::filesystem::path path = result_path();
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            return;
        }

        std::stringstream contents;
        contents << file.rdbuf();
        file.close();
        std::filesystem::remove(path, ec);

        nlohmann::json result = nlohmann::json::parse(contents.str(), nullptr, false);

        if (!result.is_object()) {
            return;
        }

        std::string version = result.value("version", std::string());
        std::string error = result.value("error", std::string());
        std::lock_guard<std::mutex> lock(mutex);

        if (!error.empty()) {
            notice = UpdateNotice{std::format("Update failed: {}", error), true};
        }
        else if (!version.empty()) {
            notice = UpdateNotice{std::format("Updated to Nascent {}", version), false};
        }
    }

    void Updater::check() {
        std::optional<ReleaseVersion> current = parse_release_version(NASCENT_VERSION_STRING);
        std::optional<InstallTarget> target = install_target();

        {
            std::lock_guard<std::mutex> lock(mutex);

            if (!current || state.status != UpdateStatus::DISABLED) {
                return;
            }

            state.status = UpdateStatus::CHECKING;
        }

        bool installable = target && writable(*target);

        UpdatePackage package = target ? target->package : UpdatePackage::ZIP;
        std::string product = package == UpdatePackage::APPIMAGE && editor_running() ? EDITOR_APPIMAGE_PRODUCT : "nascent";

        std::thread([current = *current, package, installable, product] {
            std::string error;
            bool retryable = false;
            std::string url = std::format("https://api.github.com/repos/{}/releases?per_page={}", REPOSITORY, RELEASES_PER_PAGE);
            std::optional<std::string> body = ServerClient::download(url, MAX_RELEASE_LIST_BYTES, {}, error, retryable);
            std::optional<UpdateRelease> release;

            if (body) {
                release = newest_update(nlohmann::json::parse(*body, nullptr, false), current, PACKAGE_OS, PACKAGE_ARCH, package, product);
            }
            else {
                std::cerr << "Updates: could not check for a new release: " << error << std::endl;
            }

            std::lock_guard<std::mutex> lock(mutex);
            state.release = std::move(release);
            state.installable = installable;
            state.status = state.release ? UpdateStatus::AVAILABLE : UpdateStatus::UP_TO_DATE;
        }).detach();
    }

    UpdateSnapshot Updater::snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return state;
    }

    void Updater::start() {
        std::optional<InstallTarget> target = install_target();
        UpdateRelease release;

        {
            std::lock_guard<std::mutex> lock(mutex);

            if ((state.status != UpdateStatus::AVAILABLE && state.status != UpdateStatus::FAILED) || !state.release) {
                return;
            }

            release = *state.release;

            if (!state.installable) {
                target.reset();
            }

            if (target) {
                state.status = UpdateStatus::DOWNLOADING;
                state.progress = 0;
                state.error.clear();
            }
        }

        if (!target) {
            open_url(release.page_url);
            return;
        }

        std::thread([release, target = *target] {
            std::string error;
            bool staged = stage(release, target, error);

            if (!staged) {
                std::cerr << "Updates: could not download " << release.tag << ": " << error << std::endl;
            }

            std::lock_guard<std::mutex> lock(mutex);
            state.status = staged ? UpdateStatus::READY : UpdateStatus::FAILED;
            state.error = staged ? std::string() : error;
        }).detach();
    }

    bool Updater::ready() {
        std::lock_guard<std::mutex> lock(mutex);
        return state.status == UpdateStatus::READY;
    }

    void Updater::install_and_relaunch() {
        std::optional<InstallTarget> target = install_target();
        std::string tag;

        {
            std::lock_guard<std::mutex> lock(mutex);

            if (state.status != UpdateStatus::READY || !state.release || !target) {
                return;
            }

            tag = state.release->tag;
        }

        std::string error;
        bool installed = install(*target, error);
        write_result(installed ? tag : std::string(), error);

        if (!relaunch(*target)) {
            std::cerr << "Updates: could not restart the game" << std::endl;
        }
    }

    std::optional<UpdateNotice> Updater::take_notice() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(notice, std::nullopt);
    }
}
