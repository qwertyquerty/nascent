#include "SkinArchive.h"
#include "SkinImportSupport.h"

#include "gse/Platform.h"
#include "Utf8Path.h"
#include "json.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <system_error>

namespace nascent::skin_import {
    Archive::~Archive() {
        if (open) {
            mz_zip_reader_end(&zip);
        }
    }

    std::optional<std::string> Archive::read(mz_uint index, size_t limit) {
        mz_zip_archive_file_stat stat;

        if (!mz_zip_reader_file_stat(&zip, index, &stat) || stat.m_uncomp_size > limit) {
            return std::nullopt;
        }

        std::string out((size_t)stat.m_uncomp_size, '\0');

        if (!mz_zip_reader_extract_to_mem(&zip, index, out.data(), out.size(), 0)) {
            return std::nullopt;
        }

        return out;
    }

    std::string lowercase(std::string text) {
        for (char& c : text) {
            c = (char)std::tolower((unsigned char)c);
        }

        return text;
    }

    std::string trim(std::string_view text) {
        size_t start = text.find_first_not_of(" \t\r\n");

        if (start == std::string_view::npos) {
            return std::string();
        }

        size_t end = text.find_last_not_of(" \t\r\n");
        return std::string(text.substr(start, end - start + 1));
    }

    std::string folder_of(const std::string& name) {
        size_t slash = name.rfind('/');
        return slash == std::string::npos ? std::string() : name.substr(0, slash);
    }

    std::string file_of(const std::string& name) {
        size_t slash = name.rfind('/');
        return slash == std::string::npos ? name : name.substr(slash + 1);
    }

    std::string safe_skin_name(const std::string& text) {
        std::string name;

        for (unsigned char c : text) {
            if (c >= 0x20 && c != 0x7f && std::string_view("<>:\"/\\|?*").find((char)c) == std::string_view::npos) {
                name.push_back((char)c);
            }
        }

        name = trim(name);

        while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
            name.pop_back();
        }

        while (name.size() > MAX_SKIN_NAME_BYTES) {
            name.pop_back();

            while (!name.empty() && ((unsigned char)name.back() & 0xc0) == 0x80) {
                name.pop_back();
            }

            if (!name.empty() && (unsigned char)name.back() >= 0xc0) {
                name.pop_back();
            }
        }

        return name;
    }

    std::optional<Image> load_frame(Archive& archive, const Frame& frame) {
        std::optional<std::string> data = archive.read(frame.index, MAX_SKIN_IMAGE_BYTES);

        if (!data) {
            return std::nullopt;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)data->data(), (int)data->size(), &width, &height, &channels, 4);

        if (pixels == nullptr) {
            return std::nullopt;
        }

        Image image;
        image.width = std::max(1, width / frame.columns);
        image.height = std::max(1, height / frame.rows);
        image.pixels.resize((size_t)image.width * image.height * 4);

        for (int32_t y = 0; y < image.height; y++) {
            std::copy_n(pixels + (size_t)y * width * 4, (size_t)image.width * 4, image.pixels.begin() + (ptrdiff_t)((size_t)y * image.width * 4));
        }

        stbi_image_free(pixels);
        return image;
    }

    bool write_png(const std::filesystem::path& path, const Image& image) {
        std::string encoded;
        auto append = [](void* context, void* data, int size) {
            static_cast<std::string*>(context)->append(static_cast<const char*>(data), (size_t)size);
        };

        if (!stbi_write_png_to_func(append, &encoded, image.width, image.height, 4, image.pixels.data(), image.width * 4)) {
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(encoded.data(), (std::streamsize)encoded.size());
        return (bool)out;
    }
}

namespace nascent {
    using namespace skin_import;

    namespace {
        bool is_safe_entry(const std::string& name) {
            if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos) {
                return false;
            }

            size_t start = 0;

            while (start <= name.size()) {
                size_t end = name.find('/', start);
                std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);

                if (part == "..") {
                    return false;
                }

                if (end == std::string::npos) {
                    break;
                }

                start = end + 1;
            }

            return true;
        }

        SkinImportResult import_nascent_skin(Archive& archive, const ArchiveEntries& entries, const std::string& root, const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory) {
            SkinImportResult result;
            std::error_code ec;
            std::string prefix = root.empty() ? std::string() : root + "/";
            auto manifest_entry = std::find_if(entries.begin(), entries.end(), [&](const auto& entry) {
                return entry.first == prefix + "skin.json";
            });
            std::optional<std::string> manifest_text = archive.read(manifest_entry->second, MAX_SKIN_MANIFEST_BYTES);
            nlohmann::json manifest = manifest_text ? nlohmann::json::parse(*manifest_text, nullptr, false) : nlohmann::json();

            if (manifest.is_discarded() || !manifest.is_object()) {
                result.error = "its skin.json couldn't be read";
                return result;
            }

            if (manifest.contains("inherits") && manifest["inherits"].is_string()) {
                std::string parent = manifest["inherits"].get<std::string>();

                if (!parent.empty() && !std::filesystem::is_directory(builtin_skins_directory / utf8_path(parent), ec) && !std::filesystem::is_directory(skins_directory / utf8_path(parent), ec)) {
                    result.error = std::format("it builds on the \"{}\" skin, which isn't installed", parent);
                    return result;
                }
            }

            result.name = safe_skin_name(root.empty() ? path_utf8(archive_path.stem()) : file_of(root));

            if (result.name.empty()) {
                result.name = "Imported skin";
            }

            if (std::filesystem::exists(builtin_skins_directory / utf8_path(result.name), ec)) {
                result.name += " (imported)";
            }

            std::filesystem::path destination = skins_directory / utf8_path(result.name);
            std::filesystem::path staging = destination;
            staging += ".part";
            std::filesystem::remove_all(staging, ec);
            std::filesystem::create_directories(staging, ec);

            if (ec) {
                result.error = std::format("could not create {}", path_utf8(staging));
                return result;
            }

            size_t extracted = 0;

            for (const auto& [name, index] : entries) {
                if (!name.starts_with(prefix)) {
                    continue;
                }

                std::optional<std::string> data = archive.read(index, MAX_SKIN_FILE_BYTES);
                extracted += data ? data->size() : 0;

                if (!data || extracted > MAX_SKIN_EXTRACTED_BYTES) {
                    std::filesystem::remove_all(staging, ec);
                    result.error = data ? std::format("it unpacks to more than {} MB", MAX_SKIN_EXTRACTED_BYTES / (1024 * 1024)) : std::format("{} is unreadable or larger than {} MB", file_of(name), MAX_SKIN_FILE_BYTES / (1024 * 1024));
                    return result;
                }

                std::filesystem::path target = staging / utf8_path(name.substr(prefix.size()));
                std::filesystem::create_directories(target.parent_path(), ec);
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                out.write(data->data(), (std::streamsize)data->size());

                if (!out) {
                    std::filesystem::remove_all(staging, ec);
                    result.error = std::format("could not write {}", file_of(name));
                    return result;
                }
            }

            std::filesystem::remove_all(destination, ec);

            if (!move_into_place(staging, destination, ec)) {
                std::filesystem::remove_all(staging, ec);
                result.error = std::format("could not move the skin into {}", path_utf8(skins_directory));
                return result;
            }

            result.ok = true;
            return result;
        }
    }

    bool export_skin_archive(const std::filesystem::path& skin_folder, const std::filesystem::path& archive_path, std::string& error) {
        mz_zip_archive zip = {};

        if (!mz_zip_writer_init_heap(&zip, 0, 0)) {
            error = "the archive could not be started";
            return false;
        }

        std::error_code ec;
        std::string root = path_utf8(skin_folder.filename());
        bool written = true;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(skin_folder, ec)) {
            if (!entry.is_regular_file(ec)) {
                continue;
            }

            std::string relative = path_utf8(std::filesystem::relative(entry.path(), skin_folder, ec));
            std::replace(relative.begin(), relative.end(), '\\', '/');
            std::ifstream in(entry.path(), std::ios::binary);
            std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

            if (!mz_zip_writer_add_mem(&zip, (root + "/" + relative).c_str(), data.data(), data.size(), (mz_uint)MZ_DEFAULT_LEVEL)) {
                error = std::format("{} could not be added", relative);
                written = false;
                break;
            }
        }

        void* buffer = nullptr;
        size_t size = 0;
        written = written && mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size);

        if (written) {
            std::filesystem::create_directories(archive_path.parent_path(), ec);
            std::ofstream out(archive_path, std::ios::binary | std::ios::trunc);
            out.write((const char*)buffer, (std::streamsize)size);
            written = (bool)out;
            error = written ? std::string() : std::format("{} could not be written", path_utf8(archive_path));
        }
        else if (error.empty()) {
            error = "the archive could not be finished";
        }

        mz_free(buffer);
        mz_zip_writer_end(&zip);
        return written;
    }

    SkinImportResult import_skin_archive(const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory) {
        SkinImportResult result;
        std::error_code ec;
        uintmax_t archive_size = std::filesystem::file_size(archive_path, ec);

        if (ec || archive_size > MAX_SKIN_ARCHIVE_BYTES) {
            result.error = ec ? "the file could not be read" : std::format("the file is larger than {} MB", MAX_SKIN_ARCHIVE_BYTES / (1024 * 1024));
            return result;
        }

        Archive archive;
        std::ifstream in(archive_path, std::ios::binary);
        archive.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

        if (!mz_zip_reader_init_mem(&archive.zip, archive.data.data(), archive.data.size(), 0)) {
            result.error = "the file is not a zip archive";
            return result;
        }

        archive.open = true;
        mz_uint count = mz_zip_reader_get_num_files(&archive.zip);
        ArchiveEntries entries;
        std::optional<std::string> root;
        std::optional<std::string> nascent_root;
        std::optional<std::string> osu_root;

        for (mz_uint index = 0; index < count; index++) {
            mz_zip_archive_file_stat stat;

            if (!mz_zip_reader_file_stat(&archive.zip, index, &stat) || stat.m_is_directory) {
                continue;
            }

            std::string name = stat.m_filename;
            std::replace(name.begin(), name.end(), '\\', '/');

            if (!is_safe_entry(name)) {
                continue;
            }

            std::string file = lowercase(file_of(name));

            if ((file == "noteskin.lua" || file == "metrics.ini") && (!root || folder_of(name).size() < root->size())) {
                root = folder_of(name);
            }

            if (file_of(name) == "skin.json" && (!nascent_root || folder_of(name).size() < nascent_root->size())) {
                nascent_root = folder_of(name);
            }

            if (file == "skin.ini" && (!osu_root || folder_of(name).size() < osu_root->size())) {
                osu_root = folder_of(name);
            }

            entries.emplace_back(name, index);
        }

        if (nascent_root) {
            return import_nascent_skin(archive, entries, *nascent_root, archive_path, skins_directory, builtin_skins_directory);
        }

        if (osu_root && (!root || osu_root->size() <= root->size())) {
            return import_osu_skin(archive, entries, *osu_root, archive_path, skins_directory, builtin_skins_directory);
        }

        if (!root) {
            result.error = "no skin was found in it (there is no skin.json, skin.ini, NoteSkin.lua or metrics.ini)";
            return result;
        }

        return import_etterna_skin(archive, entries, *root, archive_path, skins_directory, builtin_skins_directory);
    }
}
