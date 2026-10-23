#pragma once

#include "json.h"
#include "Utf8Path.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace nascent {
    enum class SettingsLoadStatus {
        LOADED,
        CREATED,
        MIGRATED,
        NEWER_VERSION,
        INVALID
    };

    class VersionedJsonFile {
        public:
        using Migration = std::function<void(nlohmann::json&)>;

        protected:
        std::filesystem::path path;
        uint32_t current_version;
        std::map<uint32_t, Migration> migrations;
        bool writable = true;

        public:
        VersionedJsonFile(std::filesystem::path path, uint32_t current_version, std::map<uint32_t, Migration> migrations = {});

        SettingsLoadStatus load(nlohmann::json& data, std::string& error);
        bool save(const nlohmann::json& data, std::string& error);
        void back_up_invalid_file();

        std::filesystem::path get_backup_path() const;
    };

    std::optional<std::string> read_file_bytes(const std::filesystem::path& path);
    bool replace_file(const std::filesystem::path& path, std::string_view bytes, std::string& error);

    class BinaryJsonWriter {
        public:
        std::string& out;

        void write(const nlohmann::json& value);
        void write_raw(std::string_view record);
    };

    class BinaryJsonReader {
        public:
        std::string_view data;
        size_t position = 0;
        bool ok = true;

        std::optional<std::string_view> next();
        static bool decode(std::string_view record, nlohmann::json& value);
    };

    class BinaryJsonFile {
        protected:
        std::filesystem::path path;
        uint32_t current_version;

        public:
        static constexpr std::string_view MAGIC = "NSBJ";

        BinaryJsonFile(std::filesystem::path path, uint32_t current_version);

        static std::string header(uint32_t version);
        static size_t header_size();

        std::optional<std::string> read() const;
        bool load(nlohmann::json& data) const;
        bool save(const nlohmann::json& data, std::string& error) const;
        bool save_bytes(std::string_view bytes, std::string& error) const;
        const std::filesystem::path& get_path() const;
    };

    template <typename T>
    class SettingsStore {
        protected:
        VersionedJsonFile file;
        nlohmann::json stored = nlohmann::json::object();

        public:
        T value;

        SettingsStore(std::filesystem::path path, uint32_t current_version, std::map<uint32_t, VersionedJsonFile::Migration> migrations = {})
            : file(std::move(path), current_version, std::move(migrations)) {}

        SettingsLoadStatus load(std::string& error) {
            nlohmann::json data;
            SettingsLoadStatus status = file.load(data, error);

            value = T();
            stored = nlohmann::json::object();

            if (status != SettingsLoadStatus::INVALID && status != SettingsLoadStatus::CREATED) {
                try {
                    value = data.template get<T>();
                    stored = std::move(data);
                }
                catch (const nlohmann::json::exception& e) {
                    file.back_up_invalid_file();
                    error = std::string(e.what()) + ", the file was backed up to " + path_utf8(file.get_backup_path());
                    value = T();
                    status = SettingsLoadStatus::INVALID;
                }
            }

            if (status == SettingsLoadStatus::INVALID || status == SettingsLoadStatus::CREATED || status == SettingsLoadStatus::MIGRATED) {
                std::string save_error;

                if (!save(save_error) && error.empty()) {
                    error = save_error;
                }
            }

            return status;
        }

        bool save(std::string& error) {
            nlohmann::json data = stored;
            data.update(nlohmann::json(value));

            if (!file.save(data, error)) {
                return false;
            }

            stored = std::move(data);
            return true;
        }

        const VersionedJsonFile& get_file() const {
            return file;
        }
    };
}
