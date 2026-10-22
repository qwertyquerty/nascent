#pragma once

#include "Chart.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nascent {
    class LibraryCache {
        public:
        struct Record {
            uint64_t size = 0;
            int64_t modified = 0;
            int64_t added = 0;
            uint8_t charts = 0;
            std::string audio_file;
            size_t offset = 0;
            size_t length = 0;
        };

        struct Restored {
            std::string key;
            std::unique_ptr<Chart> chart;
            int64_t added = 0;
        };

        struct Entry {
            std::string key;
            uint64_t size = 0;
            int64_t modified = 0;
            int64_t added = 0;
            std::vector<const Chart*> charts;
            bool keep = false;
        };

        static constexpr const char* FILE_NAME = "library_cache.bin";
        static constexpr uint32_t FORMAT = 2;
        static constexpr std::string_view MAGIC = "NSLC";
        static constexpr size_t MAX_CHARTS_PER_FILE = 255;

        explicit LibraryCache(std::filesystem::path path);

        std::vector<Restored> load(float pitch);
        const Record* find(const std::string& key) const;
        size_t chart_count() const;
        size_t size() const;
        bool exists() const;
        bool rewrite(const std::vector<Entry>& entries, std::string& error);
        void clear();

        private:
        std::filesystem::path path;
        std::unordered_map<std::string, Record> records;

        bool index(const std::string& data, std::vector<Restored>* restored, float pitch);
    };
}
