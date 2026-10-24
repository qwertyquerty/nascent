#include "SkinImportSupport.h"

#include "gse/Platform.h"
#include "Utf8Path.h"
#include "json.h"

#include <algorithm>
#include <array>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <string_view>
#include <system_error>
#include <vector>

namespace nascent::skin_import {
    namespace {
        constexpr int32_t MAX_REDIRECT_DEPTH = 6;
        constexpr std::array<const char*, 4> LANE_BUTTONS = {"Left", "Down", "Up", "Right"};

        struct ArchiveFile {
            std::string stem;
            std::string extension;
            mz_uint index;
        };

        constexpr double ETTERNA_ARROW_SIZE = 64.0;

        std::optional<double> metric(const std::string& metrics, const char* name) {
            std::regex pattern(std::format(R"((?:^|\n)\s*{}\s*=\s*(-?\d+(?:\.\d+)?))", name), std::regex::icase);
            std::smatch match;

            if (!std::regex_search(metrics, match, pattern)) {
                return std::nullopt;
            }

            return std::stod(match[1].str());
        }

        struct NoteSkinScript {
            std::map<std::string, std::string> button_redirects;
            std::map<std::string, int32_t> rotations = {{"left", 90}, {"down", 0}, {"up", 180}, {"right", -90}};
            std::map<std::string, bool> rotated_parts = {{"tap note", true}, {"receptor", true}, {"hold head active", true}};
        };

        std::string normalize_name(std::string_view text) {
            std::string name = lowercase(trim(text));

            while (name.starts_with('_')) {
                name.erase(name.begin());
            }

            std::string collapsed;

            for (char c : name) {
                if (c != ' ' || (!collapsed.empty() && collapsed.back() != ' ')) {
                    collapsed.push_back(c);
                }
            }

            return trim(collapsed);
        }

        void parse_script(const std::string& text, NoteSkinScript& script) {
            auto table = [&](const char* name) -> std::optional<std::string> {
                std::smatch match;
                std::regex pattern(std::string(name) + R"(\s*=\s*\{([^}]*)\})");

                if (std::regex_search(text, match, pattern)) {
                    return match[1].str();
                }

                return std::nullopt;
            };

            if (std::optional<std::string> body = table("ButtonRedir")) {
                std::regex pair(R"((\w+)\s*=\s*\"([^\"]*)\")");

                for (auto it = std::sregex_iterator(body->begin(), body->end(), pair); it != std::sregex_iterator(); ++it) {
                    script.button_redirects[lowercase((*it)[1].str())] = (*it)[2].str();
                }
            }

            if (std::optional<std::string> body = table("Rotate")) {
                std::regex pair(R"((\w+)\s*=\s*(-?\d+))");
                script.rotations.clear();

                for (auto it = std::sregex_iterator(body->begin(), body->end(), pair); it != std::sregex_iterator(); ++it) {
                    script.rotations[lowercase((*it)[1].str())] = std::stoi((*it)[2].str());
                }
            }

            if (std::optional<std::string> body = table("PartsToRotate")) {
                std::regex pair(R"(\[\s*\"([^\"]+)\"\s*\]\s*=\s*(true|false))");
                script.rotated_parts.clear();

                for (auto it = std::sregex_iterator(body->begin(), body->end(), pair); it != std::sregex_iterator(); ++it) {
                    script.rotated_parts[normalize_name((*it)[1].str())] = (*it)[2].str() == "true";
                }
            }
        }

        class NoteSkinFiles {
            public:
            Archive& archive;
            std::vector<ArchiveFile> files;

            NoteSkinFiles(Archive& archive) : archive(archive) {}

            std::vector<Frame> resolve(const std::string& wanted, int32_t depth = 0) {
                if (depth > MAX_REDIRECT_DEPTH) {
                    return {};
                }

                std::string target = normalize_name(wanted);
                static const std::regex suffix(R"(^(\s+(\d+)x(\d+))?(\s*\(.*\))?\s*$)");

                for (const ArchiveFile& file : files) {
                    if (file.extension != ".png" || !file.stem.starts_with(target)) {
                        continue;
                    }

                    std::smatch match;
                    std::string rest = file.stem.substr(target.size());

                    if (!std::regex_match(rest, match, suffix)) {
                        continue;
                    }

                    Frame frame{file.index};

                    if (match[2].matched) {
                        frame.columns = std::max(1, std::stoi(match[2].str()));
                        frame.rows = std::max(1, std::stoi(match[3].str()));
                    }

                    static const std::regex resolution(R"(res\s*(\d+)\s*x\s*(\d+))", std::regex::icase);
                    std::string tag = match[4].str();
                    std::smatch size;

                    if (std::regex_search(tag, size, resolution)) {
                        frame.res_width = std::stoi(size[1].str());
                        frame.res_height = std::stoi(size[2].str());
                    }

                    return {frame};
                }

                for (const ArchiveFile& file : files) {
                    if (file.stem != target || (file.extension != ".redir" && file.extension != ".lua")) {
                        continue;
                    }

                    std::optional<std::string> text = archive.read(file.index, 64 * 1024);

                    if (!text) {
                        continue;
                    }

                    if (file.extension == ".redir") {
                        return resolve(*text, depth + 1);
                    }

                    std::vector<Frame> frames;
                    std::regex get_path(R"(GetPath\s*\(\s*\"([^\"]*)\"\s*,\s*\"([^\"]*)\"\s*\))", std::regex::icase);

                    for (auto it = std::sregex_iterator(text->begin(), text->end(), get_path); it != std::sregex_iterator(); ++it) {
                        std::vector<Frame> found = resolve((*it)[1].str() + " " + (*it)[2].str(), depth + 1);
                        frames.insert(frames.end(), found.begin(), found.end());
                    }

                    if (!frames.empty()) {
                        return frames;
                    }
                }

                return {};
            }
        };

        Image rotate(const Image& image, int32_t degrees) {
            int32_t turns = ((degrees / 90) % 4 + 4) % 4;

            if (turns == 0) {
                return image;
            }

            Image rotated;
            rotated.width = turns % 2 == 0 ? image.width : image.height;
            rotated.height = turns % 2 == 0 ? image.height : image.width;
            rotated.pixels.resize(image.pixels.size());

            for (int32_t y = 0; y < image.height; y++) {
                for (int32_t x = 0; x < image.width; x++) {
                    int32_t tx = x;
                    int32_t ty = y;

                    if (turns == 1) {
                        tx = image.height - 1 - y;
                        ty = x;
                    }
                    else if (turns == 2) {
                        tx = image.width - 1 - x;
                        ty = image.height - 1 - y;
                    }
                    else {
                        tx = y;
                        ty = image.width - 1 - x;
                    }

                    std::copy_n(image.pixels.begin() + (ptrdiff_t)(((size_t)y * image.width + x) * 4), 4, rotated.pixels.begin() + (ptrdiff_t)(((size_t)ty * rotated.width + tx) * 4));
                }
            }

            return rotated;
        }
    }

    SkinImportResult import_etterna_skin(Archive& archive, const ArchiveEntries& entries, const std::string& root, const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory) {
        SkinImportResult result;
        std::error_code ec;
        NoteSkinFiles skin(archive);
        NoteSkinScript script;
        std::string metrics;

        for (const auto& [name, index] : entries) {
            if (folder_of(name) != root) {
                continue;
            }

            std::string file = file_of(name);
            size_t dot = file.rfind('.');
            std::string stem = dot == std::string::npos ? file : file.substr(0, dot);
            std::string extension = dot == std::string::npos ? std::string() : lowercase(file.substr(dot));

            if (lowercase(file) == "noteskin.lua") {
                if (std::optional<std::string> text = archive.read(index, 256 * 1024)) {
                    parse_script(*text, script);
                }
            }

            if (lowercase(file) == "metrics.ini") {
                metrics = archive.read(index, 256 * 1024).value_or("");
            }

            skin.files.push_back({normalize_name(stem), extension, index});
        }

        std::string base_name = root.empty() ? path_utf8(archive_path.stem()) : file_of(root);
        result.name = safe_skin_name(base_name);

        if (result.name.empty()) {
            result.name = "Etterna skin";
        }

        if (std::filesystem::exists(builtin_skins_directory / utf8_path(result.name), ec)) {
            result.name += " (Etterna)";
        }

        auto lane_frames = [&](size_t lane, const char* element) {
            std::string button = LANE_BUTTONS[lane];
            auto redirect = script.button_redirects.find(lowercase(button));
            std::string source = redirect != script.button_redirects.end() ? redirect->second : button;
            std::vector<Frame> frames = skin.resolve(source + " " + element);

            if (frames.empty() && lowercase(source) != "down") {
                frames = skin.resolve(std::string("Down ") + element);
            }

            return frames;
        };

        auto lane_rotation = [&](size_t lane, const char* element) {
            auto part = script.rotated_parts.find(normalize_name(element));

            if (part == script.rotated_parts.end() || !part->second) {
                return 0;
            }

            auto rotation = script.rotations.find(lowercase(LANE_BUTTONS[lane]));
            return rotation != script.rotations.end() ? rotation->second : 0;
        };

        std::filesystem::path destination = skins_directory / utf8_path(result.name);
        std::filesystem::path staging = destination;
        staging += ".part";
        std::filesystem::remove_all(staging, ec);
        std::filesystem::create_directories(staging, ec);

        if (ec) {
            result.error = std::format("could not create {}", path_utf8(staging));
            return result;
        }

        struct Output {
            const char* element;
            const char* file;
            size_t frame;
            bool rotates;
            bool required;
        };

        static constexpr std::array<Output, 6> OUTPUTS = {{
            {"Tap Note", "note", 0, true, true},
            {"Receptor", "receptor", 0, true, false},
            {"Receptor", "receptor_pressed", 1, true, false},
            {"Hold Head Active", "hold_head", 0, true, false},
            {"Hold Body Active", "hold_body", 0, false, false},
            {"Hold BottomCap Active", "hold_cap", 0, false, false},
        }};

        for (const Output& output : OUTPUTS) {
            for (size_t lane = 0; lane < LANE_BUTTONS.size(); lane++) {
                std::vector<Frame> frames = lane_frames(lane, output.element);

                if (std::string_view(output.element) == "Hold Head Active" && frames.empty()) {
                    frames = lane_frames(lane, "Hold Head");
                }

                std::optional<Image> image = output.frame < frames.size() ? load_frame(archive, frames[output.frame]) : std::nullopt;

                if (!image) {
                    if (output.required) {
                        std::filesystem::remove_all(staging, ec);
                        result.error = std::format("it has no readable {} image for the {} lane", output.element, LANE_BUTTONS[lane]);
                        return result;
                    }

                    continue;
                }

                if (output.rotates) {
                    *image = rotate(*image, lane_rotation(lane, output.element));
                }

                if (!write_png(staging / std::format("{}_{}.png", output.file, lane + 1), *image)) {
                    std::filesystem::remove_all(staging, ec);
                    result.error = "could not write the converted images";
                    return result;
                }
            }
        }

        nlohmann::json config = {
            {"format", 1},
            {"name", result.name},
            {"source", "etterna"},
            {"inherits", "default"},
            {"tint", "none"},
            {"hold_style", "tile"},
            {"receptor_alpha", 255},
        };

        for (size_t lane = 0; lane < LANE_BUTTONS.size(); lane++) {
            std::vector<Frame> frames = lane_frames(lane, "Hold Body Active");

            if (frames.empty()) {
                continue;
            }

            const Frame& body = frames.front();

            if (body.res_width > 0 && body.res_height > 0) {
                config["hold_body_aspect"] = ((double)body.res_height / body.rows) / ((double)body.res_width / body.columns);
            }

            break;
        }

        if (std::optional<double> stretch = metric(metrics, "UseStretchHolds"); stretch && *stretch != 0) {
            config["hold_style"] = "stretch";
        }

        if (std::optional<double> head = metric(metrics, "StartDrawingHoldBodyOffsetFromHead"); head && *head != 0) {
            config["hold_body_head_offset"] = *head / ETTERNA_ARROW_SIZE;
        }

        if (std::optional<double> tail = metric(metrics, "StopDrawingHoldBodyOffsetFromTail"); tail && *tail != 0) {
            config["hold_body_tail_offset"] = *tail / ETTERNA_ARROW_SIZE;
        }
        std::ofstream(staging / "skin.json", std::ios::trunc) << config.dump(4) << std::endl;

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
