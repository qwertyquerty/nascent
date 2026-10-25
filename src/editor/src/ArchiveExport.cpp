#include "ArchiveExport.h"

#include "ChartDocument.h"
#include "OsuExport.h"
#include "Utf8Path.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <miniz.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace nascent {
    namespace {
        struct Entry {
            std::string name;
            std::vector<char> data;
            bool compress = true;
        };

        std::optional<std::vector<char>> read_file(const std::filesystem::path& file) {
            std::ifstream in(file, std::ios::binary);

            if (!in) {
                return std::nullopt;
            }

            return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        std::filesystem::path stored_path(const std::string& stored, const std::filesystem::path& folder) {
            std::filesystem::path path = utf8_path(stored);
            return path.is_absolute() ? path : folder / path;
        }
    }

    bool export_archive(const ChartData& data, const std::filesystem::path& folder, const std::filesystem::path& target, ArchiveFormat format, const OsuDifficulty& difficulty, std::string& error) {
        ChartData packed = data;
        std::vector<Entry> entries;

        if (data.info.audio_file.empty()) {
            error = "choose the chart's audio before exporting an archive";
            return false;
        }

        std::filesystem::path audio = stored_path(data.info.audio_file, folder);
        std::optional<std::vector<char>> audio_data = read_file(audio);

        if (!audio_data) {
            error = "audio file " + path_utf8(audio) + " is missing";
            return false;
        }

        packed.info.audio_file = path_utf8(audio.filename());
        entries.push_back({packed.info.audio_file, std::move(*audio_data), false});

        if (!data.info.image_file.empty()) {
            std::filesystem::path image = stored_path(data.info.image_file, folder);

            if (std::optional<std::vector<char>> image_data = read_file(image)) {
                packed.info.image_file = path_utf8(image.filename());

                if (packed.info.image_file != packed.info.audio_file) {
                    entries.push_back({packed.info.image_file, std::move(*image_data), false});
                }
            }
            else {
                packed.info.image_file.clear();
            }
        }

        std::string stem = path_utf8(target.stem());
        std::string chart = format == ArchiveFormat::OSZ ? chart_to_osu(packed, difficulty) : chart_to_json(packed);
        entries.insert(entries.begin(), {stem + (format == ArchiveFormat::OSZ ? ".osu" : ".json"), std::vector<char>(chart.begin(), chart.end()), true});

        mz_zip_archive zip = {};

        if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
            error = "could not start the archive";
            return false;
        }

        bool written = true;

        for (const Entry& entry : entries) {
            mz_uint level = entry.compress ? (mz_uint)MZ_DEFAULT_LEVEL : (mz_uint)MZ_NO_COMPRESSION;
            written = written && mz_zip_writer_add_mem(&zip, entry.name.c_str(), entry.data.data(), entry.data.size(), level);
        }

        void* buffer = nullptr;
        size_t size = 0;
        written = written && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);

        if (written) {
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            out.write((const char*)buffer, (std::streamsize)size);
            written = (bool)out;
        }

        mz_free(buffer);
        mz_zip_writer_end(&zip);

        if (!written) {
            error = "could not write " + path_utf8(target);
            return false;
        }

        return true;
    }
}
