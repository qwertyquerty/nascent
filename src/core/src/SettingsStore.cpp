#include "SettingsStore.h"
#include "Utf8Path.h"

#include <cstring>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace nascent {
    std::optional<std::string> read_file_bytes(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file) {
            return std::nullopt;
        }

        std::streamoff size = file.tellg();

        if (size < 0) {
            return std::nullopt;
        }

        std::string bytes((size_t)size, '\0');
        file.seekg(0);

        if (size > 0 && !file.read(bytes.data(), size)) {
            return std::nullopt;
        }

        return bytes;
    }

    bool replace_file(const std::filesystem::path& path, std::string_view bytes, std::string& error) {
        std::error_code ec;

        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }

        std::filesystem::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), (std::streamsize)bytes.size());

            if (!file) {
                error = std::format("could not write {}", path_utf8(temporary));
                return false;
            }
        }

        std::filesystem::rename(temporary, path, ec);

        if (ec) {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(temporary, path, ec);
        }

        if (ec) {
            error = std::format("could not replace {}: {}", path_utf8(path), ec.message());
            return false;
        }

        return true;
    }

    void BinaryJsonWriter::write(const nlohmann::json& value) {
        std::vector<uint8_t> bytes = nlohmann::json::to_msgpack(value);
        write_raw(std::string_view((const char*)bytes.data(), bytes.size()));
    }

    void BinaryJsonWriter::write_raw(std::string_view record) {
        uint32_t length = (uint32_t)record.size();
        out.append((const char*)&length, sizeof(length));
        out.append(record);
    }

    std::optional<std::string_view> BinaryJsonReader::next() {
        uint32_t length = 0;

        if (!ok || data.size() - position < sizeof(length)) {
            ok = false;
            return std::nullopt;
        }

        std::memcpy(&length, data.data() + position, sizeof(length));
        position += sizeof(length);

        if (data.size() - position < length) {
            ok = false;
            return std::nullopt;
        }

        std::string_view record = data.substr(position, length);
        position += length;
        return record;
    }

    bool BinaryJsonReader::decode(std::string_view record, nlohmann::json& value) {
        value = nlohmann::json::from_msgpack(record.begin(), record.end(), true, false);
        return !value.is_discarded();
    }

    BinaryJsonFile::BinaryJsonFile(std::filesystem::path path, uint32_t current_version) : path(std::move(path)), current_version(current_version) {}

    std::string BinaryJsonFile::header(uint32_t version) {
        std::string bytes(MAGIC);
        bytes.append((const char*)&version, sizeof(version));
        return bytes;
    }

    size_t BinaryJsonFile::header_size() {
        return MAGIC.size() + sizeof(uint32_t);
    }

    std::optional<std::string> BinaryJsonFile::read() const {
        std::optional<std::string> bytes = read_file_bytes(path);

        if (!bytes || bytes->size() < header_size() || std::string_view(*bytes).substr(0, header_size()) != header(current_version)) {
            return std::nullopt;
        }

        return bytes;
    }

    bool BinaryJsonFile::load(nlohmann::json& data) const {
        std::optional<std::string> bytes = read();

        if (!bytes) {
            return false;
        }

        BinaryJsonReader reader{*bytes, header_size()};
        std::optional<std::string_view> record = reader.next();
        return record && BinaryJsonReader::decode(*record, data);
    }

    bool BinaryJsonFile::save(const nlohmann::json& data, std::string& error) const {
        std::string bytes = header(current_version);
        BinaryJsonWriter{bytes}.write(data);
        return save_bytes(bytes, error);
    }

    bool BinaryJsonFile::save_bytes(std::string_view bytes, std::string& error) const {
        return replace_file(path, bytes, error);
    }

    const std::filesystem::path& BinaryJsonFile::get_path() const {
        return path;
    }

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
            error = std::format("could not open {}", path_utf8(path));
            writable = false;
            return SettingsLoadStatus::INVALID;
        }

        nlohmann::json parsed = nlohmann::json::parse(ifs, nullptr, false);

        if (parsed.is_discarded() || !parsed.is_object()) {
            error = std::format("{} is not a valid settings file, it was backed up to {}", path_utf8(path), path_utf8(get_backup_path()));
            back_up_invalid_file();
            return SettingsLoadStatus::INVALID;
        }

        uint32_t version = 1;

        if (parsed.contains("version")) {
            if (!parsed["version"].is_number_unsigned()) {
                error = std::format("{} has an invalid version, it was backed up to {}", path_utf8(path), path_utf8(get_backup_path()));
                back_up_invalid_file();
                return SettingsLoadStatus::INVALID;
            }

            version = parsed["version"].get<uint32_t>();
            parsed.erase("version");
        }

        if (version > current_version) {
            error = std::format("{} was written by a newer version (settings version {}, this build supports {}), changes will not be saved", path_utf8(path), version, current_version);
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
                error = std::format("no migration from settings version {} to {}, {} was backed up to {}", from, from + 1, path_utf8(path), path_utf8(get_backup_path()));
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
            error = std::format("{} was not saved because it belongs to a newer version", path_utf8(path));
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
                error = std::format("could not write {}", path_utf8(temporary));
                return false;
            }

            ofs << output.dump(4) << std::endl;

            if (!ofs) {
                error = std::format("could not write {}", path_utf8(temporary));
                return false;
            }
        }

        std::filesystem::rename(temporary, path, ec);

        if (ec) {
            std::filesystem::remove(path, ec);
            std::filesystem::rename(temporary, path, ec);
        }

        if (ec) {
            error = std::format("could not replace {}: {}", path_utf8(path), ec.message());
            return false;
        }

        return true;
    }
}
