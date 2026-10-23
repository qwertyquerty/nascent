#include "LibraryCache.h"

#include "SettingsStore.h"
#include "Utf8Path.h"
#include "gse/Platform.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <mutex>
#include <random>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nascent {
    namespace {
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

        enum ChartField : size_t {
            TITLE,
            TITLE_UNICODE,
            ARTIST,
            ARTIST_UNICODE,
            MAPPER,
            DIFFICULTY_NAME,
            AUDIO_FILE,
            IMAGE_FILE,
            AUDIO_LEAD_IN,
            PREVIEW_TIME,
            HIT_ACCURACY,
            KEY_COUNT,
            DAMAGE,
            GAMEPLAY_HASH,
            DIFFICULTY_KEY,
            TIMING,
            NOTE_COUNT,
            HOLD_COUNT,
            START_MS,
            END_MS,
            BPM_MAIN,
            BPM_MIN,
            BPM_MAX,
            NOTES_OFFSET,
            NOTES_LENGTH,
            CHART_FIELDS
        };

        enum FileField : size_t {
            FILE_KEY,
            FILE_SIZE,
            FILE_MODIFIED,
            FILE_ADDED,
            FILE_CHARTS,
            FILE_FIELDS
        };

        constexpr size_t HIT_BYTES = 10;
        constexpr size_t TIMING_BYTES = 9;

        std::string fingerprint() {
            return std::format("{}\n{}", Chart::CHART_HASH_FORMAT, LibraryCache::FORMAT);
        }

        std::string legacy_fingerprint() {
            return std::format("{}\n{}", Chart::CHART_HASH_FORMAT, LibraryCache::LEGACY_FORMAT);
        }

        template <typename T>
        void put(std::vector<uint8_t>& out, T value) {
            static_assert(std::is_trivially_copyable_v<T>);
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }

        template <typename T>
        T take(const std::vector<uint8_t>& in, size_t& position) {
            static_assert(std::is_trivially_copyable_v<T>);
            T value{};
            std::memcpy(&value, in.data() + position, sizeof(T));
            position += sizeof(T);
            return value;
        }

        std::vector<uint8_t> pack_hits(const std::vector<Hit>& hits) {
            std::vector<uint8_t> bytes;
            bytes.reserve(hits.size() * HIT_BYTES);

            for (const Hit& hit : hits) {
                put(bytes, hit.key);
                put(bytes, (uint8_t)hit.hit_type);
                put(bytes, hit.time);
                put(bytes, hit.end_time);
            }

            return bytes;
        }

        std::optional<std::vector<Hit>> unpack_hits(const std::vector<uint8_t>& bytes) {
            if (bytes.size() % HIT_BYTES != 0) {
                return std::nullopt;
            }

            std::vector<Hit> hits(bytes.size() / HIT_BYTES);
            size_t position = 0;

            for (Hit& hit : hits) {
                hit.key = take<uint8_t>(bytes, position);
                hit.hit_type = (HitType)take<uint8_t>(bytes, position);
                hit.time = take<int32_t>(bytes, position);
                hit.end_time = take<int32_t>(bytes, position);
            }

            return hits;
        }

        std::vector<uint8_t> pack_timing(const std::vector<ChartTimingPoint>& timing) {
            std::vector<uint8_t> bytes;
            bytes.reserve(timing.size() * TIMING_BYTES);

            for (const ChartTimingPoint& point : timing) {
                put(bytes, point.time);
                put(bytes, point.beat_length);
                put(bytes, point.meter);
            }

            return bytes;
        }

        std::optional<std::vector<ChartTimingPoint>> unpack_timing(const std::vector<uint8_t>& bytes) {
            if (bytes.size() % TIMING_BYTES != 0) {
                return std::nullopt;
            }

            std::vector<ChartTimingPoint> timing(bytes.size() / TIMING_BYTES);
            size_t position = 0;

            for (ChartTimingPoint& point : timing) {
                point.time = take<int32_t>(bytes, position);
                point.beat_length = take<float>(bytes, position);
                point.meter = take<uint8_t>(bytes, position);
            }

            return timing;
        }

        nlohmann::json chart_json(const Chart& chart, const ChartSummary& summary, const LibraryCache::NoteLocation& location) {
            const ChartInfo& info = chart.unpitched_chart_info();
            nlohmann::json fields = nlohmann::json::array();
            nlohmann::json::array_t& values = fields.get_ref<nlohmann::json::array_t&>();
            values.reserve(CHART_FIELDS);

            for (const std::string* text : {&info.title, &info.title_unicode, &info.artist, &info.artist_unicode, &info.mapper, &info.difficulty_name, &info.audio_file, &info.image_file}) {
                values.emplace_back(*text);
            }

            values.emplace_back(info.audio_lead_in);
            values.emplace_back(info.preview_time);
            values.emplace_back(info.hit_accuracy);
            values.emplace_back(info.key_count);
            values.emplace_back(info.damage);
            values.emplace_back(chart.gameplay_hash());
            values.emplace_back(chart.difficulty_key());
            values.emplace_back(nlohmann::json::binary(pack_timing(chart.unpitched_timing())));
            values.emplace_back(summary.notes);
            values.emplace_back(summary.holds);
            values.emplace_back(summary.start_ms);
            values.emplace_back(summary.end_ms);
            values.emplace_back(summary.bpm.main);
            values.emplace_back(summary.bpm.min);
            values.emplace_back(summary.bpm.max);
            values.emplace_back(location.offset);
            values.emplace_back(location.length);
            return fields;
        }

        uint64_t new_notes_id() {
            std::random_device device;
            uint64_t id = ((uint64_t)device() << 32) ^ (uint64_t)device() ^ (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
            return id == 0 ? 1 : id;
        }

        std::string notes_header(uint64_t id) {
            std::string bytes = BinaryJsonFile::header(LibraryCache::FORMAT);
            BinaryJsonWriter{bytes}.write({{"id", id}});
            return bytes;
        }

        std::optional<std::string> read_range(const std::filesystem::path& path, uint64_t offset, uint64_t length) {
            std::ifstream file(path, std::ios::binary);

            if (!file || !file.seekg((std::streamoff)offset)) {
                return std::nullopt;
            }

            std::string bytes((size_t)length, ' ');

            if (!file.read(bytes.data(), (std::streamsize)length)) {
                return std::nullopt;
            }

            return bytes;
        }

        std::optional<uint64_t> read_notes_id(const std::filesystem::path& path) {
            size_t header_size = BinaryJsonFile::header_size();
            std::optional<std::string> header = read_range(path, 0, header_size + sizeof(uint32_t));

            if (!header || std::string_view(*header).substr(0, header_size) != BinaryJsonFile::header(LibraryCache::FORMAT)) {
                return std::nullopt;
            }

            uint32_t length = 0;
            std::memcpy(&length, header->data() + header_size, sizeof(length));
            std::optional<std::string> record = read_range(path, header_size, sizeof(uint32_t) + (uint64_t)length);
            nlohmann::json value;

            if (!record || !BinaryJsonReader::decode(std::string_view(*record).substr(sizeof(uint32_t)), value) || !value.is_object()) {
                return std::nullopt;
            }

            return value.value("id", (uint64_t)0);
        }

        std::string encode_notes(const std::vector<Hit>& hits) {
            std::string record;
            BinaryJsonWriter{record}.write(nlohmann::json::binary(pack_hits(hits)));
            return record;
        }

        std::optional<std::vector<Hit>> decode_notes(std::string_view bytes) {
            BinaryJsonReader reader{bytes};
            std::optional<std::string_view> record = reader.next();
            nlohmann::json value;

            if (!record || !BinaryJsonReader::decode(*record, value) || !value.is_binary()) {
                return std::nullopt;
            }

            return unpack_hits(value.get_binary());
        }

        bool rename_into_place(const std::filesystem::path& from, const std::filesystem::path& to, std::string& error) {
            std::error_code ec;

            if (!move_into_place(from, to, ec)) {
                error = std::format("could not replace {}: {}", path_utf8(to), ec.message());
                return false;
            }

            return true;
        }

        std::unique_ptr<Chart> read_legacy_chart(Reader& reader, const std::string& key, float pitch, std::string& audio_file) {
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

            if (!reader.ok || hit_count > (reader.data.size() - reader.position) / HIT_BYTES) {
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

            if (!reader.ok || timing_count > (reader.data.size() - reader.position) / TIMING_BYTES) {
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
            return Chart::restore(utf8_path(key), std::move(info), std::move(hits), std::move(timing), std::move(gameplay_hash), std::move(difficulty_key), pitch);
        }
    }

    ChartSummary summarize_chart(const Chart& chart) {
        ChartSummary summary;
        const std::vector<Hit>& hits = chart.unpitched_hit_objects();
        summary.notes = (uint32_t)hits.size();
        summary.start_ms = hits.empty() ? 0 : hits.front().time;

        for (const Hit& hit : hits) {
            summary.holds += hit.hit_type == HitType::HOLD ? 1 : 0;
            summary.end_ms = std::max(summary.end_ms, hit.hit_type == HitType::HOLD ? hit.end_time : hit.time);
        }

        summary.bpm = chart.bpm_range();
        return summary;
    }

    LibraryCache::LibraryCache(std::filesystem::path directory)
        : index_path(directory / INDEX_FILE_NAME), notes_path(directory / NOTES_FILE_NAME), legacy_path(directory / LEGACY_FILE_NAME) {}

    std::vector<LibraryCache::Restored> LibraryCache::load(float pitch) {
        std::optional<std::string> bytes = BinaryJsonFile(index_path, FORMAT).read();

        if (!bytes) {
            return migrate_legacy(pitch);
        }

        std::vector<Restored> restored;
        std::unordered_map<std::string, Record> loaded;
        BinaryJsonReader reader{*bytes, BinaryJsonFile::header_size()};
        std::optional<std::string_view> header_record = reader.next();
        nlohmann::json header;
        uint64_t id = 0;

        auto fail = [&]() {
            std::unique_lock lock(mutex);
            records.clear();
            notes_id = 0;
            notes_size = 0;
            return std::vector<Restored>();
        };

        if (!header_record || !BinaryJsonReader::decode(*header_record, header) || !header.is_object() || header.value("fingerprint", std::string()) != fingerprint()) {
            return fail();
        }

        id = header.value("notes_id", (uint64_t)0);

        if (id == 0 || read_notes_id(notes_path) != id) {
            return fail();
        }

        try {
            while (reader.ok && reader.position < bytes->size()) {
                size_t start = reader.position;
                std::optional<std::string_view> entry_bytes = reader.next();
                nlohmann::json entry;

                if (!entry_bytes || !BinaryJsonReader::decode(*entry_bytes, entry) || !entry.is_array() || entry.size() != FILE_FIELDS || !entry[FILE_CHARTS].is_array()) {
                    return fail();
                }

                std::string key = entry[FILE_KEY].get<std::string>();
                Record record;
                record.size = entry[FILE_SIZE].get<uint64_t>();
                record.modified = entry[FILE_MODIFIED].get<int64_t>();
                record.added = entry[FILE_ADDED].get<int64_t>();
                record.charts = (uint8_t)std::min(entry[FILE_CHARTS].size(), MAX_CHARTS_PER_FILE);
                record.index_offset = start;
                record.index_length = reader.position - start;

                for (const nlohmann::json& fields : entry[FILE_CHARTS]) {
                    if (!fields.is_array() || fields.size() != CHART_FIELDS || !fields[TIMING].is_binary()) {
                        return fail();
                    }

                    ChartInfo info;
                    info.title = fields[TITLE].get<std::string>();
                    info.title_unicode = fields[TITLE_UNICODE].get<std::string>();
                    info.artist = fields[ARTIST].get<std::string>();
                    info.artist_unicode = fields[ARTIST_UNICODE].get<std::string>();
                    info.mapper = fields[MAPPER].get<std::string>();
                    info.difficulty_name = fields[DIFFICULTY_NAME].get<std::string>();
                    info.audio_file = fields[AUDIO_FILE].get<std::string>();
                    info.image_file = fields[IMAGE_FILE].get<std::string>();
                    info.audio_lead_in = fields[AUDIO_LEAD_IN].get<int32_t>();
                    info.preview_time = fields[PREVIEW_TIME].get<int32_t>();
                    info.hit_accuracy = fields[HIT_ACCURACY].get<uint8_t>();
                    info.key_count = fields[KEY_COUNT].get<uint8_t>();
                    info.damage = fields[DAMAGE].get<uint8_t>();
                    std::optional<std::vector<ChartTimingPoint>> timing = unpack_timing(fields[TIMING].get_binary());

                    if (!timing) {
                        return fail();
                    }

                    ChartSummary summary;
                    summary.notes = fields[NOTE_COUNT].get<uint32_t>();
                    summary.holds = fields[HOLD_COUNT].get<uint32_t>();
                    summary.start_ms = fields[START_MS].get<int32_t>();
                    summary.end_ms = fields[END_MS].get<int32_t>();
                    summary.bpm = {fields[BPM_MAIN].get<double>(), fields[BPM_MIN].get<double>(), fields[BPM_MAX].get<double>()};
                    NoteLocation location{fields[GAMEPLAY_HASH].get<std::string>(), fields[NOTES_OFFSET].get<uint64_t>(), fields[NOTES_LENGTH].get<uint32_t>()};

                    if (record.notes.empty()) {
                        record.audio_file = info.audio_file;
                    }

                    if (pitch > 0) {
                        std::unique_ptr<Chart> chart = Chart::restore(utf8_path(key), std::move(info), {}, std::move(*timing), location.gameplay_hash, fields[DIFFICULTY_KEY].get<std::string>(), pitch);

                        if (chart != nullptr) {
                            chart->release_notes();
                            restored.push_back({key, std::move(chart), record.added, summary});
                        }
                    }

                    record.notes.push_back(std::move(location));
                }

                loaded.emplace(std::move(key), std::move(record));
            }
        }
        catch (const nlohmann::json::exception&) {
            return fail();
        }

        if (!reader.ok) {
            return fail();
        }

        std::error_code ec;
        uint64_t size = std::filesystem::file_size(notes_path, ec);
        std::unique_lock lock(mutex);
        records = std::move(loaded);
        notes_id = id;
        notes_size = ec ? 0 : size;
        return restored;
    }

    std::vector<LibraryCache::Restored> LibraryCache::migrate_legacy(float pitch) {
        std::optional<std::string> data = read_file_bytes(legacy_path);

        {
            std::unique_lock lock(mutex);
            records.clear();
            notes_id = 0;
            notes_size = 0;
        }

        if (!data || data->size() < LEGACY_MAGIC.size() || std::string_view(*data).substr(0, LEGACY_MAGIC.size()) != LEGACY_MAGIC) {
            return {};
        }

        Reader header{*data};
        header.position = LEGACY_MAGIC.size();

        if (header.value<uint32_t>() != LEGACY_FORMAT || header.text() != legacy_fingerprint()) {
            return {};
        }

        std::vector<Restored> restored;
        std::vector<std::unique_ptr<Chart>> owned;
        std::vector<Entry> entries;
        uint32_t count = header.value<uint32_t>();

        for (uint32_t index = 0; index < count && header.ok; index++) {
            uint32_t length = header.value<uint32_t>();

            if (!header.ok || data->size() - header.position < length) {
                return {};
            }

            Reader entry{std::string_view(*data).substr(header.position, length)};
            Entry migrated;
            migrated.key = entry.text();
            migrated.size = entry.value<uint64_t>();
            migrated.modified = entry.value<int64_t>();
            migrated.added = entry.value<int64_t>();
            uint8_t charts = entry.value<uint8_t>();

            for (uint8_t chart_index = 0; chart_index < charts; chart_index++) {
                std::string audio_file;
                std::unique_ptr<Chart> chart = read_legacy_chart(entry, migrated.key, pitch > 0 ? pitch : 1.0f, audio_file);

                if (!entry.ok) {
                    return {};
                }

                if (chart != nullptr) {
                    migrated.charts.push_back(chart.get());
                    owned.push_back(std::move(chart));
                }
            }

            if (!entry.ok) {
                return {};
            }

            header.position += length;
            entries.push_back(std::move(migrated));
        }

        if (!header.ok) {
            return {};
        }

        std::string error;

        if (rewrite(entries, error)) {
            std::error_code ec;
            std::filesystem::remove(legacy_path, ec);
        }

        size_t next = 0;

        for (const Entry& entry : entries) {
            for (size_t chart_index = 0; chart_index < entry.charts.size(); chart_index++) {
                std::unique_ptr<Chart>& chart = owned[next++];
                ChartSummary summary = summarize_chart(*chart);
                chart->release_notes();

                if (pitch > 0) {
                    restored.push_back({entry.key, std::move(chart), entry.added, summary});
                }
            }
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
        return std::filesystem::exists(index_path, ec);
    }

    bool LibraryCache::rewrite(const std::vector<Entry>& entries, std::string& error) {
        std::optional<std::string> old_index;
        uint64_t id = notes_id;
        uint64_t size = notes_size;
        bool fresh = id == 0 || read_notes_id(notes_path) != id;

        if (!fresh && std::any_of(entries.begin(), entries.end(), [](const Entry& entry) { return entry.keep; })) {
            old_index = BinaryJsonFile(index_path, FORMAT).read();
        }

        if (fresh) {
            {
                std::unique_lock lock(mutex);
                records.clear();
                notes_id = 0;
                notes_size = 0;
            }

            id = new_notes_id();
            std::string header = notes_header(id);

            if (!replace_file(notes_path, header, error)) {
                return false;
            }

            size = header.size();
        }

        std::unordered_map<std::string, Record> next;
        std::vector<std::string> order;
        std::string body;
        BinaryJsonWriter writer{body};

        {
            std::ofstream notes(notes_path, std::ios::binary | std::ios::app);

            if (!notes) {
                error = std::format("could not write {}", path_utf8(notes_path));
                return false;
            }

            for (const Entry& entry : entries) {
                const Record* old = fresh ? nullptr : find(entry.key);

                if (entry.keep && old != nullptr && old_index && old->index_length >= sizeof(uint32_t) && old->index_offset + old->index_length <= old_index->size()) {
                    Record copy = *old;
                    copy.index_offset = body.size();
                    writer.write_raw(std::string_view(*old_index).substr(old->index_offset + sizeof(uint32_t), old->index_length - sizeof(uint32_t)));
                    copy.index_length = body.size() - copy.index_offset;
                    order.push_back(entry.key);
                    next[entry.key] = std::move(copy);
                    continue;
                }

                if (entry.keep) {
                    continue;
                }

                Record record;
                record.size = entry.size;
                record.modified = entry.modified;
                record.added = entry.added;
                size_t chart_count = std::min(entry.charts.size(), MAX_CHARTS_PER_FILE);
                record.charts = (uint8_t)chart_count;
                nlohmann::json charts = nlohmann::json::array();

                for (size_t chart_index = 0; chart_index < chart_count; chart_index++) {
                    const Chart& chart = *entry.charts[chart_index];
                    std::string blob = encode_notes(chart.unpitched_hit_objects());
                    notes.write(blob.data(), (std::streamsize)blob.size());
                    NoteLocation location{chart.gameplay_hash(), size, (uint32_t)blob.size()};
                    size += blob.size();
                    charts.push_back(chart_json(chart, summarize_chart(chart), location));

                    if (chart_index == 0) {
                        record.audio_file = chart.unpitched_chart_info().audio_file;
                    }

                    record.notes.push_back(std::move(location));
                }

                record.index_offset = body.size();
                writer.write(nlohmann::json::array({entry.key, entry.size, entry.modified, entry.added, std::move(charts)}));
                record.index_length = body.size() - record.index_offset;
                order.push_back(entry.key);
                next[entry.key] = std::move(record);
            }

            notes.flush();

            if (!notes) {
                error = std::format("could not write {}", path_utf8(notes_path));
                return false;
            }
        }

        uint64_t live = notes_header(id).size();

        for (const auto& [key, record] : next) {
            for (const NoteLocation& location : record.notes) {
                live += location.length;
            }
        }

        bool compacting = size > compact_min_bytes && live * 2 < size;
        std::filesystem::path compacted_path = notes_path;
        compacted_path += ".tmp";

        if (compacting) {
            uint64_t compacted_id = new_notes_id();
            std::string compacted_header = notes_header(compacted_id);
            uint64_t compacted_size = compacted_header.size();
            std::string rebuilt;
            BinaryJsonWriter rebuilt_writer{rebuilt};
            std::ifstream source(notes_path, std::ios::binary);
            std::ofstream compacted(compacted_path, std::ios::binary | std::ios::trunc);
            compacted.write(compacted_header.data(), (std::streamsize)compacted_header.size());

            for (const std::string& key : order) {
                Record& record = next[key];
                nlohmann::json entry;

                if (!BinaryJsonReader::decode(std::string_view(body).substr(record.index_offset + sizeof(uint32_t), record.index_length - sizeof(uint32_t)), entry) || !entry.is_array() || entry.size() != FILE_FIELDS) {
                    error = "could not compact the library cache";
                    return false;
                }

                for (size_t chart_index = 0; chart_index < record.notes.size() && chart_index < entry[FILE_CHARTS].size(); chart_index++) {
                    NoteLocation& location = record.notes[chart_index];
                    std::string bytes((size_t)location.length, ' ');

                    if (!source.seekg((std::streamoff)location.offset) || !source.read(bytes.data(), (std::streamsize)bytes.size())) {
                        error = "could not compact the library cache";
                        return false;
                    }

                    location.offset = compacted_size;
                    compacted.write(bytes.data(), (std::streamsize)bytes.size());
                    compacted_size += bytes.size();
                    entry[FILE_CHARTS][chart_index][NOTES_OFFSET] = location.offset;
                }

                record.index_offset = rebuilt.size();
                rebuilt_writer.write(entry);
                record.index_length = rebuilt.size() - record.index_offset;
            }

            source.close();
            compacted.flush();

            if (!compacted) {
                error = std::format("could not write {}", path_utf8(compacted_path));
                return false;
            }

            compacted.close();
            id = compacted_id;
            size = compacted_size;
            body = std::move(rebuilt);
        }

        std::string index = BinaryJsonFile::header(FORMAT);
        BinaryJsonWriter{index}.write({{"fingerprint", fingerprint()}, {"notes_id", id}});
        size_t prefix = index.size();
        index.append(body);

        for (auto& [key, record] : next) {
            record.index_offset += prefix;
        }

        std::filesystem::path index_temporary = index_path;
        index_temporary += ".next";

        if (!replace_file(index_temporary, index, error)) {
            return false;
        }

        std::unique_lock lock(mutex);

        if (compacting && !rename_into_place(compacted_path, notes_path, error)) {
            return false;
        }

        if (!rename_into_place(index_temporary, index_path, error)) {
            return false;
        }

        records = std::move(next);
        notes_id = id;
        notes_size = size;
        return true;
    }

    void LibraryCache::clear() {
        std::unique_lock lock(mutex);
        records.clear();
        notes_id = 0;
        notes_size = 0;
        std::error_code ec;
        std::filesystem::remove(index_path, ec);
        std::filesystem::remove(notes_path, ec);
        std::filesystem::remove(legacy_path, ec);
    }

    std::optional<std::vector<Hit>> LibraryCache::load_notes(const Chart& chart) const {
        std::shared_lock lock(mutex);
        auto record = records.find(path_utf8(chart.file_path));

        if (record == records.end()) {
            return std::nullopt;
        }

        for (const NoteLocation& location : record->second.notes) {
            if (location.gameplay_hash != chart.gameplay_hash()) {
                continue;
            }

            std::optional<std::string> bytes = read_range(notes_path, location.offset, location.length);
            return bytes ? decode_notes(*bytes) : std::nullopt;
        }

        return std::nullopt;
    }
}
