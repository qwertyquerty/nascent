#include "SettingsStore.h"

#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace nascent {
    VersionedJsonFile::VersionedJsonFile(std::filesystem::path path, uint32_t current_version, std::map<uint32_t, Migration> migrations)
        : path(std::move(path)), current_version(current_version), migrations(std::move(migrations)) {}

    std::filesystem::path VersionedJsonFile::get_backup_path() const {
        std::filesystem::path backup = path;
        backup.replace_extension(".invalid.json");
        return backup;
    }

    void VersionedJsonFile::back_up_invalid_file() {
        std::error_code ec;
        std::filesystem::copy_file(path, get_backup_path(), std::filesystem::copy_options::overwrite_existing, ec);
    }

    SettingsLoadStatus VersionedJsonFile::load(nlohmann::json& data, std::string& error) {
        writable = true;
        data = nlohmann::json::object();

        std::error_code ec;

        if (!std::filesystem::exists(path, ec)) {
            return SettingsLoadStatus::CREATED;
        }

        std::ifstream ifs(path);

        if (!ifs) {
            error = std::format("could not open {}", path.string());
            writable = false;
            return SettingsLoadStatus::INVALID;
        }

        nlohmann::json parsed = nlohmann::json::parse(ifs, nullptr, false);

        if (parsed.is_discarded() || !parsed.is_object()) {
            error = std::format("{} is not a valid settings file, it was backed up to {}", path.string(), get_backup_path().string());
            back_up_invalid_file();
            return SettingsLoadStatus::INVALID;
        }

        uint32_t version = 1;

        if (parsed.contains("version")) {
            if (!parsed["version"].is_number_unsigned()) {
                error = std::format("{} has an invalid version, it was backed up to {}", path.string(), get_backup_path().string());
                back_up_invalid_file();
                return SettingsLoadStatus::INVALID;
            }

            version = parsed["version"].get<uint32_t>();
            parsed.erase("version");
        }

        if (version > current_version) {
            error = std::format("{} was written by a newer version (settings version {}, this build supports {}), changes will not be saved", path.string(), version, current_version);
            writable = false;
            data = std::move(parsed);
            return SettingsLoadStatus::NEWER_VERSION;
        }

        if (version == current_version) {
            data = std::move(parsed);
            return SettingsLoadStatus::LOADED;
        }

        for (uint32_t from = version; from < current_version; from++) {
            auto migration = migrations.find(from);

            if (migration == migrations.end()) {
                error = std::format("no migration from settings version {} to {}, {} was backed up to {}", from, from + 1, path.string(), get_backup_path().string());
                back_up_invalid_file();
                return SettingsLoadStatus::INVALID;
            }

            migration->second(parsed);
        }

        data = std::move(parsed);
        return SettingsLoadStatus::MIGRATED;
    }

    bool VersionedJsonFile::save(const nlohmann::json& data, std::string& error) {
        if (!writable) {
            error = std::format("{} was not saved because it belongs to a newer version", path.string());
            return false;
        }

        nlohmann::ordered_json output = nlohmann::ordered_json::object();
        output["version"] = current_version;

        for (const auto& [key, value] : data.items()) {
            if (key != "version") {
                output[key] = value;
            }
        }

        std::error_code ec;

        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::filesystem::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream ofs(temporary, std::ios::trunc);

            if (!ofs) {
                error = std::format("could not write {}", temporary.string());
                return false;
            }

            ofs << output.dump(4) << std::endl;

            if (!ofs) {
                error = std::format("could not write {}", temporary.string());
                return false;
            }
        }

        std::filesystem::rename(temporary, path, ec);

        if (ec) {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(temporary, path, ec);
        }

        if (ec) {
            error = std::format("could not replace {}: {}", path.string(), ec.message());
            return false;
        }

        return true;
    }
}
