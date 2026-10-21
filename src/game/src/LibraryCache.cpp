#include "LibraryCache.h"

#include "Utf8Path.h"
#include "gse/Platform.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nascent {
    namespace {
        class Writer {
            public:
            std::string& out;

            template <typename T>
            void value(T number) {
                static_assert(std::is_trivially_copyable_v<T>);
                const char* bytes = reinterpret_cast<const char*>(&number);
                out.append(bytes, sizeof(T));
            }

            void text(std::string_view text) {
                value((uint32_t)text.size());
                out.append(text.data(), text.size());
            }
        };

        class Reader {
            public:
            std::string_view data;
            size_t position = 0;
            bool ok = true;

            template <typename T>
            T value() {
                static_assert(std::is_trivially_copyable_v<T>);
                T number{};

                if (!ok || data.size() - position < sizeof(T)) {
                    ok = false;
                    return number;
                }

                std::memcpy(&number, data.data() + position, sizeof(T));
                position += sizeof(T);
                return number;
            }

            std::string text() {
                uint32_t length = value<uint32_t>();

                if (!ok || data.size() - position < length) {
                    ok = false;
                    return {};
                }

                std::string result(data.substr(position, length));
                position += length;
                return result;
            }
        };

        std::string fingerprint() {
            return std::format("{}\n{}", Chart::CHART_HASH_FORMAT, LibraryCache::FORMAT);
        }

        void write_chart(Writer& writer, const Chart& chart) {
            const ChartInfo& info = chart.unpitched_chart_info();

            for (const std::string* text : {&info.title, &info.title_unicode, &info.artist, &info.artist_unicode, &info.mapper, &info.difficulty_name, &info.audio_file, &info.image_file}) {
                writer.text(*text);
            }

            writer.value(info.audio_lead_in);
            writer.value(info.preview_time);
            writer.value(info.hit_accuracy);
            writer.value(info.key_count);
            writer.value(info.damage);
            writer.text(chart.gameplay_hash());
            writer.text(chart.difficulty_key());
            writer.value((uint32_t)chart.unpitched_hit_objects().size());

            for (const Hit& hit : chart.unpitched_hit_objects()) {
                writer.value(hit.key);
                writer.value((uint8_t)hit.hit_type);
                writer.value(hit.time);
                writer.value(hit.end_time);
            }

            writer.value((uint32_t)chart.unpitched_timing().size());

            for (const ChartTimingPoint& point : chart.unpitched_timing()) {
                writer.value(point.time);
                writer.value(point.beat_length);
                writer.value(point.meter);
            }
        }

        std::unique_ptr<Chart> read_chart(Reader& reader, const std::string& key, float pitch, std::string& audio_file) {
            ChartInfo info;

            for (std::string* text : {&info.title, &info.title_unicode, &info.artist, &info.artist_unicode, &info.mapper, &info.difficulty_name, &info.audio_file, &info.image_file}) {
                *text = reader.text();
            }

            info.audio_lead_in = reader.value<int32_t>();
            info.preview_time = reader.value<int32_t>();
            info.hit_accuracy = reader.value<uint8_t>();
            info.key_count = reader.value<uint8_t>();
            info.damage = reader.value<uint8_t>();
            std::string gameplay_hash = reader.text();
            std::string difficulty_key = reader.text();
            uint32_t hit_count = reader.value<uint32_t>();

            if (!reader.ok || hit_count > (reader.data.size() - reader.position) / 10) {
                reader.ok = false;
                return nullptr;
            }

            std::vector<Hit> hits(hit_count);

            for (Hit& hit : hits) {
                hit.key = reader.value<uint8_t>();
                hit.hit_type = (HitType)reader.value<uint8_t>();
                hit.time = reader.value<int32_t>();
                hit.end_time = reader.value<int32_t>();
            }

            uint32_t timing_count = reader.value<uint32_t>();

            if (!reader.ok || timing_count > (reader.data.size() - reader.position) / 9) {
                reader.ok = false;
                return nullptr;
            }

            std::vector<ChartTimingPoint> timing(timing_count);

            for (ChartTimingPoint& point : timing) {
                point.time = reader.value<int32_t>();
                point.beat_length = reader.value<float>();
                point.meter = reader.value<uint8_t>();
            }

            if (!reader.ok) {
                return nullptr;
            }

            audio_file = info.audio_file;
            return pitch > 0 ? Chart::restore(utf8_path(key), std::move(info), std::move(hits), std::move(timing), std::move(gameplay_hash), std::move(difficulty_key), pitch) : nullptr;
        }

        std::optional<std::string> read_file(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);

            if (!file) {
                return std::nullopt;
            }

            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }
    }

    LibraryCache::LibraryCache(std::filesystem::path path) : path(std::move(path)) {}

    bool LibraryCache::index(const std::string& data, std::vector<Restored>* restored, float pitch) {
        records.clear();
        Reader header{data};

        if (data.size() < MAGIC.size() || std::string_view(data).substr(0, MAGIC.size()) != MAGIC) {
            return false;
        }

        header.position = MAGIC.size();

        if (header.value<uint32_t>() != FORMAT || header.text() != fingerprint()) {
            return false;
        }

        uint32_t count = header.value<uint32_t>();

        for (uint32_t index = 0; index < count && header.ok; index++) {
            uint32_t length = header.value<uint32_t>();

            if (!header.ok || data.size() - header.position < length) {
                return false;
            }

            Record record;
            record.offset = header.position;
            record.length = length;
            Reader entry{std::string_view(data).substr(header.position, length)};
            std::string key = entry.text();
            record.size = entry.value<uint64_t>();
            record.modified = entry.value<int64_t>();
            record.added = entry.value<int64_t>();
            record.charts = entry.value<uint8_t>();

            for (uint8_t chart_index = 0; chart_index < record.charts; chart_index++) {
                std::string audio_file;
                std::unique_ptr<Chart> chart = read_chart(entry, key, restored != nullptr ? pitch : 0.0f, audio_file);

                if (!entry.ok) {
                    return false;
                }

                if (chart_index == 0) {
                    record.audio_file = std::move(audio_file);
                }

                if (chart != nullptr && restored != nullptr) {
                    restored->push_back({key, std::move(chart), record.added});
                }
            }

            if (!entry.ok) {
                return false;
            }

            header.position += length;
            records.emplace(std::move(key), std::move(record));
        }

        return header.ok;
    }

    std::vector<LibraryCache::Restored> LibraryCache::load(float pitch) {
        std::vector<Restored> restored;
        std::optional<std::string> data = read_file(path);

        if (!data || !index(*data, &restored, pitch)) {
            records.clear();
            restored.clear();
        }

        return restored;
    }

    const LibraryCache::Record* LibraryCache::find(const std::string& key) const {
        auto found = records.find(key);
        return found != records.end() ? &found->second : nullptr;
    }

    size_t LibraryCache::chart_count() const {
        size_t count = 0;

        for (const auto& [key, record] : records) {
            count += record.charts > 0 ? 1 : 0;
        }

        return count;
    }

    size_t LibraryCache::size() const {
        return records.size();
    }

    bool LibraryCache::exists() const {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    bool LibraryCache::rewrite(const std::vector<Entry>& entries, std::string& error) {
        std::optional<std::string> old;

        if (std::any_of(entries.begin(), entries.end(), [](const Entry& entry) { return entry.keep; })) {
            old = read_file(path);
        }

        std::string out;
        Writer writer{out};
        out.append(MAGIC);
        writer.value(FORMAT);
        writer.text(fingerprint());
        size_t count_position = out.size();
        writer.value((uint32_t)0);
        uint32_t count = 0;

        for (const Entry& entry : entries) {
            const Record* record = find(entry.key);

            if (entry.keep && record != nullptr && old && record->offset + record->length <= old->size()) {
                writer.value((uint32_t)record->length);
                out.append(*old, record->offset, record->length);
                count++;
                continue;
            }

            if (entry.keep) {
                continue;
            }

            std::string body;
            Writer item{body};
            item.text(entry.key);
            item.value(entry.size);
            item.value(entry.modified);
            item.value(entry.added);
            size_t chart_count = std::min(entry.charts.size(), MAX_CHARTS_PER_FILE);
            item.value((uint8_t)chart_count);

            for (size_t chart_index = 0; chart_index < chart_count; chart_index++) {
                write_chart(item, *entry.charts[chart_index]);
            }

            writer.value((uint32_t)body.size());
            out.append(body);
            count++;
        }

        std::memcpy(out.data() + count_position, &count, sizeof(count));
        std::filesystem::path temporary = path;
        temporary += ".tmp";
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.write(out.data(), (std::streamsize)out.size());

            if (!file) {
                error = std::format("could not write {}", path_utf8(temporary));
                return false;
            }
        }

        if (!move_into_place(temporary, path, ec)) {
            error = std::format("could not replace {}: {}", path_utf8(path), ec.message());
            return false;
        }

        if (!index(out, nullptr, 0.0f)) {
            records.clear();
        }

        return true;
    }

    void LibraryCache::clear() {
        records.clear();
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}
