#include "ChartFolders.h"

#include "ChartLibrary.h"
#include "Const.h"
#include "SettingsManager.h"
#include "Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string_view>

namespace nascent {
    namespace {
        std::atomic<bool> folders_changed = false;

        std::filesystem::path normalized(const std::filesystem::path& folder) {
            std::error_code ec;
            std::filesystem::path canonical = std::filesystem::weakly_canonical(folder, ec);
            std::filesystem::path result = (ec ? folder : canonical).lexically_normal();
            return result.has_filename() || !result.has_parent_path() ? result : result.parent_path();
        }

        bool inside(const std::filesystem::path& path, const std::filesystem::path& folder) {
            auto [end, unused] = std::mismatch(folder.begin(), folder.end(), path.begin(), path.end());
            return end == folder.end();
        }

        bool has_chart_files(const std::filesystem::path& folder) {
            std::error_code ec;

            for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
                if (is_library_chart_file(entry.path())) {
                    return true;
                }
            }

            return false;
        }

        bool holds_song_folders(const std::filesystem::path& folder, bool or_packs) {
            std::error_code ec;
            size_t scanned = 0;

            for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
                if (!entry.is_directory(ec)) {
                    continue;
                }

                if (has_chart_files(entry.path()) || (or_packs && holds_song_folders(entry.path(), false))) {
                    return true;
                }

                if (++scanned >= ChartFolders::SCAN_FOLDER_LIMIT) {
                    break;
                }
            }

            return false;
        }

        std::string_view trim_spaces(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
                text.remove_prefix(1);
            }

            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
                text.remove_suffix(1);
            }

            return text;
        }

        std::optional<std::filesystem::path> configured_osu_songs(const std::filesystem::path& osu) {
            std::error_code ec;

            for (const auto& entry : std::filesystem::directory_iterator(osu, ec)) {
                std::string name = path_utf8(entry.path().filename());

                if (!name.starts_with("osu!.") || !name.ends_with(".cfg") || name == "osu!.cfg") {
                    continue;
                }

                std::ifstream file(entry.path());
                std::string line;

                while (std::getline(file, line)) {
                    std::string_view text = trim_spaces(line);
                    size_t equals = text.find('=');

                    if (equals == std::string_view::npos || trim_spaces(text.substr(0, equals)) != ChartFolders::OSU_BEATMAP_SETTING) {
                        continue;
                    }

                    std::string_view value = trim_spaces(text.substr(equals + 1));

                    if (!value.empty()) {
                        return osu / utf8_path(std::string(value));
                    }
                }
            }

            return std::nullopt;
        }

        std::optional<std::filesystem::path> local_app_data() {
#ifdef _WIN32
            const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
            return local != nullptr && *local != L'\0' ? std::optional<std::filesystem::path>(std::filesystem::path(local)) : std::nullopt;
#else
            return std::nullopt;
#endif
        }
    }

    std::vector<std::filesystem::path> ChartFolders::all() {
        std::vector<std::filesystem::path> folders = {SettingsManager::song_directory()};
        std::error_code ec;

        for (const std::string& folder : SettingsManager::settings.list_linked_directories) {
            std::filesystem::path path = utf8_path(folder);

            if (std::filesystem::is_directory(path, ec)) {
                folders.push_back(path);
            }
        }

        return folders;
    }

    bool ChartFolders::is_linked(const std::filesystem::path& folder) {
        std::filesystem::path target = normalized(folder);

        return std::any_of(SettingsManager::settings.list_linked_directories.begin(), SettingsManager::settings.list_linked_directories.end(), [&](const std::string& linked) {
            return normalized(utf8_path(linked)) == target;
        });
    }

    bool ChartFolders::in_linked_folder(const std::filesystem::path& file) {
        std::filesystem::path target = normalized(file);

        return std::any_of(SettingsManager::settings.list_linked_directories.begin(), SettingsManager::settings.list_linked_directories.end(), [&](const std::string& linked) {
            return inside(target, normalized(utf8_path(linked)));
        });
    }

    FolderLinkResult ChartFolders::link(const std::filesystem::path& folder) {
        std::error_code ec;

        if (!std::filesystem::is_directory(folder, ec)) {
            return {false, "That folder doesn't exist"};
        }

        std::filesystem::path target = normalized(folder);
        std::string name = display_name(path_utf8(target));

        if (inside(target, normalized(SettingsManager::song_directory()))) {
            return {false, "That folder is already part of your Nascent songs folder"};
        }

        if (std::any_of(SettingsManager::settings.list_linked_directories.begin(), SettingsManager::settings.list_linked_directories.end(), [&](const std::string& linked) {
                return inside(target, normalized(utf8_path(linked)));
            })) {
            return {false, std::format("{} is already linked", name)};
        }

        if (has_chart_files(target)) {
            return {false, "That folder holds a single song. Link the folder that contains your song folders instead"};
        }

        if (!holds_song_folders(target, true)) {
            return {false, std::format("No charts were found in {}", name)};
        }

        SettingsManager::settings.list_linked_directories.push_back(path_utf8(target));
        SettingsManager::save();
        folders_changed = true;
        return {true, std::format("Linked {}, its charts are loading into the song list", name)};
    }

    FolderLinkResult ChartFolders::unlink(size_t index) {
        std::vector<std::string>& linked = SettingsManager::settings.list_linked_directories;

        if (index >= linked.size()) {
            return {false, "There's no linked folder to unlink"};
        }

        std::string name = display_name(linked[index]);
        linked.erase(linked.begin() + (std::ptrdiff_t)index);
        SettingsManager::save();
        folders_changed = true;
        return {true, std::format("Unlinked {}, its files were left where they are", name)};
    }

    std::optional<std::filesystem::path> ChartFolders::osu_songs_folder() {
        std::optional<std::filesystem::path> local = local_app_data();

        if (!local) {
            return std::nullopt;
        }

        std::filesystem::path osu = *local / OSU_FOLDER_NAME;
        std::error_code ec;

        for (std::optional<std::filesystem::path> candidate : {configured_osu_songs(osu), std::optional<std::filesystem::path>(osu / OSU_DEFAULT_SONGS)}) {
            if (candidate && std::filesystem::is_directory(*candidate, ec)) {
                return normalized(*candidate);
            }
        }

        return std::nullopt;
    }

    std::optional<std::filesystem::path> ChartFolders::etterna_songs_folder() {
        std::vector<std::filesystem::path> installs;
#ifdef _WIN32
        installs.push_back(std::filesystem::path(L"C:\\Games") / ETTERNA_FOLDER_NAME);

        for (const wchar_t* variable : {L"ProgramFiles", L"ProgramFiles(x86)"}) {
            const wchar_t* value = _wgetenv(variable);

            if (value != nullptr && *value != L'\0') {
                installs.push_back(std::filesystem::path(value) / ETTERNA_FOLDER_NAME);
            }
        }
#endif
        std::error_code ec;

        for (const std::filesystem::path& install : installs) {
            std::filesystem::path songs = install / ETTERNA_SONGS;

            if (std::filesystem::is_directory(songs, ec)) {
                return normalized(songs);
            }
        }

        return std::nullopt;
    }

    std::string ChartFolders::display_name(const std::string& folder) {
        std::filesystem::path path = normalized(utf8_path(folder));
        std::filesystem::path parent = path.parent_path();

        if (!parent.has_filename()) {
            return path_utf8(path);
        }

        std::filesystem::path shown = parent.filename() / path.filename();
        return (parent.has_parent_path() && parent.parent_path().has_filename() ? "…" + std::string(1, (char)std::filesystem::path::preferred_separator) : std::string()) + path_utf8(shown.make_preferred());
    }

    bool ChartFolders::take_changed() {
        return folders_changed.exchange(false);
    }
}
