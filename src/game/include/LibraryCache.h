#pragma once

#include "Chart.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nascent {
    struct ChartSummary {
        uint32_t notes = 0;
        uint32_t holds = 0;
        int32_t start_ms = 0;
        int32_t end_ms = 0;
        BpmRange bpm;
    };

    ChartSummary summarize_chart(const Chart& chart);

    class LibraryCache {
        public:
        struct NoteLocation {
            std::string gameplay_hash;
            uint64_t offset = 0;
            uint32_t length = 0;
        };

        struct Record {
            uint64_t size = 0;
            int64_t modified = 0;
            int64_t added = 0;
            uint8_t charts = 0;
            std::string audio_file;
            std::vector<NoteLocation> notes;
            size_t index_offset = 0;
            size_t index_length = 0;
        };

        struct Restored {
            std::string key;
            std::unique_ptr<Chart> chart;
            int64_t added = 0;
            ChartSummary summary;
        };

        struct Entry {
            std::string key;
            uint64_t size = 0;
            int64_t modified = 0;
            int64_t added = 0;
            std::vector<const Chart*> charts;
            bool keep = false;
        };

        static constexpr const char* INDEX_FILE_NAME = "library_index.bin";
        static constexpr const char* NOTES_FILE_NAME = "library_notes.bin";
        static constexpr const char* LEGACY_FILE_NAME = "library_cache.bin";
        static constexpr uint32_t FORMAT = 4;
        static constexpr uint32_t LEGACY_FORMAT = 3;
        static constexpr std::string_view LEGACY_MAGIC = "NSLC";
        static constexpr size_t MAX_CHARTS_PER_FILE = 255;
        static constexpr uint64_t COMPACT_MIN_BYTES = 16ull * 1024 * 1024;

        uint64_t compact_min_bytes = COMPACT_MIN_BYTES;

        explicit LibraryCache(std::filesystem::path directory);

        std::vector<Restored> load(float pitch);
        const Record* find(const std::string& key) const;
        size_t chart_count() const;
        size_t size() const;
        bool exists() const;
        bool rewrite(const std::vector<Entry>& entries, std::string& error);
        void clear();
        std::optional<std::vector<Hit>> load_notes(const Chart& chart) const;

        private:
        std::filesystem::path index_path;
        std::filesystem::path notes_path;
        std::filesystem::path legacy_path;
        mutable std::shared_mutex mutex;
        std::unordered_map<std::string, Record> records;
        uint64_t notes_id = 0;
        uint64_t notes_size = 0;

        std::vector<Restored> migrate_legacy(float pitch);
    };
}
