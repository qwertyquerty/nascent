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
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace nascent::skin_import {
    namespace {
        constexpr double OSU_CANVAS_HEIGHT = 480.0;
        constexpr double OSU_STAGE_HEIGHT = 768.0;
        constexpr double OSU_STAGE_HINT_SCALE = 0.9 * 1.6025;
        constexpr double OSU_DEFAULT_COLUMN_WIDTH = 30.0;
        constexpr double OSU_DEFAULT_HIT_POSITION = 402.0;
        constexpr double OSU_DEFAULT_COLUMN_LINE_WIDTH = 2.0;
        constexpr double OSU_MIN_HIT_POSITION = 240.0;
        constexpr double OSU_DEFAULT_LIGHT_POSITION = 413.0;
        constexpr double OSU_DEFAULT_BARLINE_HEIGHT = 1.2;
        constexpr double OSU_DEFAULT_LIGHT_FPS = 60.0;
        constexpr double OSU_DEFAULT_FPS = 60.0;
        constexpr double OSU_JUDGEMENT_FPS = 20.0;
        constexpr double OSU_HOLD_BODY_FRAME_MS = 30.0;
        constexpr double OSU_LIGHTING_MAX_FPS = 60.0;
        constexpr double OSU_LIGHTING_MS = 170.0;
        constexpr double OSU_STAGE_BOTTOM_SCALE = 1.6;
        constexpr double OSU_REFERENCE_HEIGHT = 1080.0;
        constexpr double OSU_BODY_STYLE_VERSION = 2.5;
        constexpr double OSU_COMBO_POP_FROM = 1.2;
        constexpr double OSU_COMBO_POP_SPEED = 3.3;
        constexpr double OSU_JUDGEMENT_POP_TO = 0.75;
        constexpr double OSU_JUDGEMENT_POP_SPEED = 12.0;
        constexpr double OSU_JUDGEMENT_FADE_DELAY = 0.18;
        constexpr double OSU_JUDGEMENT_FADE_SPEED = 25.0;
        constexpr int32_t OSU_COMBO_BREAK_MIN = 21;
        constexpr int32_t OSU_MAX_KEYS = 10;
        constexpr int32_t OSU_MAX_FRAMES = 60;
        constexpr uint8_t OSU_JUDGEMENT_LINE_ALPHA = 230;
        constexpr std::array<uint8_t, 4> OSU_WHITE = {255, 255, 255, 255};
        constexpr std::array<uint8_t, 4> OSU_DEFAULT_HOLD_COLOUR = {255, 191, 51, 255};
        constexpr std::array<uint8_t, 4> OSU_DEFAULT_BREAK_COLOUR = {255, 0, 0, 255};
        constexpr std::array<uint8_t, 4> OSU_DEFAULT_KEY_WARNING_COLOUR = {0, 0, 0, 255};

        using OsuValues = std::map<std::string, std::string>;

        struct OsuSkinIni {
            OsuValues general;
            OsuValues fonts;
            std::map<int32_t, OsuValues> mania;
        };

        OsuSkinIni parse_osu_skin_ini(std::string text) {
            OsuSkinIni ini;

            if (text.starts_with("\xEF\xBB\xBF")) {
                text.erase(0, 3);
            }

            std::string section;
            OsuValues values;

            auto finish_section = [&]() {
                if (section == "mania") {
                    auto keys = values.find("keys");
                    int32_t count = keys != values.end() ? std::atoi(keys->second.c_str()) : 0;

                    if (count >= 1 && count <= OSU_MAX_KEYS) {
                        ini.mania[count] = values;
                    }
                }

                values.clear();
            };

            std::istringstream stream(text);
            std::string raw;

            while (std::getline(stream, raw)) {
                std::string line = trim(raw);

                if (line.empty() || line.starts_with("//")) {
                    continue;
                }

                if (line.front() == '[' && line.back() == ']') {
                    finish_section();
                    section = lowercase(line.substr(1, line.size() - 2));
                    continue;
                }

                size_t colon = line.find(':');

                if (colon == std::string::npos) {
                    continue;
                }

                std::string key = lowercase(trim(std::string_view(line).substr(0, colon)));
                std::string value = trim(std::string_view(line).substr(colon + 1));

                if (section == "general") {
                    ini.general[key] = value;
                }
                else if (section == "fonts") {
                    ini.fonts[key] = value;
                }
                else if (section == "mania") {
                    values[key] = value;
                }
            }

            finish_section();
            return ini;
        }

        std::optional<std::string> osu_value(const OsuValues& values, const std::string& key) {
            auto found = values.find(key);
            return found != values.end() && !found->second.empty() ? std::optional<std::string>(found->second) : std::nullopt;
        }

        std::vector<double> osu_numbers(const std::string& text) {
            std::vector<double> numbers;
            std::istringstream stream(text);
            std::string part;

            while (std::getline(stream, part, ',')) {
                std::string value = trim(part);

                if (!value.empty()) {
                    numbers.push_back(std::strtod(value.c_str(), nullptr));
                }
            }

            return numbers;
        }

        std::optional<double> osu_number(const OsuValues& values, const std::string& key) {
            std::optional<std::string> value = osu_value(values, key);
            std::vector<double> numbers = value ? osu_numbers(*value) : std::vector<double>();
            return numbers.empty() ? std::nullopt : std::optional<double>(numbers.front());
        }

        std::optional<std::array<uint8_t, 4>> osu_colour(const OsuValues& values, const std::string& key) {
            std::optional<std::string> value = osu_value(values, key);
            std::vector<double> parts = value ? osu_numbers(*value) : std::vector<double>();

            if (parts.size() < 3) {
                return std::nullopt;
            }

            std::array<uint8_t, 4> colour = {};

            for (size_t channel = 0; channel < colour.size(); channel++) {
                colour[channel] = (uint8_t)std::clamp(channel < parts.size() ? parts[channel] : 255.0, 0.0, 255.0);
            }

            return colour;
        }

        std::string osu_image_name(std::string name) {
            std::replace(name.begin(), name.end(), '\\', '/');
            name = lowercase(trim(name));

            if (name.ends_with(".png")) {
                name.resize(name.size() - 4);
            }

            return name;
        }

        struct OsuFiles {
            std::map<std::string, mz_uint> images;
            std::map<std::string, std::pair<mz_uint, std::string>> sounds;
            std::set<mz_uint> doubled;

            double scale(mz_uint index) const {
                return doubled.contains(index) ? 2.0 : 1.0;
            }

            std::optional<mz_uint> image(const std::string& name) const {
                for (const std::string& candidate : {name + "@2x", name}) {
                    auto found = images.find(candidate);

                    if (found != images.end()) {
                        return found->second;
                    }
                }

                return std::nullopt;
            }

            std::vector<mz_uint> frames(const std::string& name) const {
                std::vector<mz_uint> found;

                for (int32_t frame = 0; frame < OSU_MAX_FRAMES; frame++) {
                    std::optional<mz_uint> index = image(std::format("{}-{}", name, frame));

                    if (!index) {
                        break;
                    }

                    found.push_back(*index);
                }

                if (found.empty()) {
                    if (std::optional<mz_uint> single = image(name)) {
                        found.push_back(*single);
                    }
                }

                return found;
            }
        };

        Image tinted(Image image, const std::array<uint8_t, 4>& colour) {
            for (size_t index = 0; index < image.pixels.size(); index++) {
                image.pixels[index] = (uint8_t)(image.pixels[index] * colour[index % 4] / 255);
            }

            return image;
        }

        Image solid(int32_t width, int32_t height, const std::array<uint8_t, 4>& colour) {
            Image image;
            image.width = width;
            image.height = height;
            image.pixels.resize((size_t)width * height * 4);

            for (size_t index = 0; index < image.pixels.size(); index++) {
                image.pixels[index] = colour[index % 4];
            }

            return image;
        }

        Image with_line(Image image, const std::array<uint8_t, 4>& colour) {
            int32_t row = image.height / 2;
            double alpha = colour[3] / 255.0;

            for (int32_t x = 0; x < image.width; x++) {
                uint8_t* pixel = image.pixels.data() + ((size_t)row * image.width + x) * 4;
                double below = pixel[3] / 255.0;
                double out = alpha + below * (1.0 - alpha);

                for (int32_t channel = 0; channel < 3; channel++) {
                    double mixed = out > 0 ? (colour[channel] * alpha + pixel[channel] * below * (1.0 - alpha)) / out : 0.0;
                    pixel[channel] = (uint8_t)std::clamp(std::lround(mixed), 0L, 255L);
                }

                pixel[3] = (uint8_t)std::clamp(std::lround(out * 255.0), 0L, 255L);
            }

            return image;
        }

        double rounded(double value) {
            return std::round(value * 100.0) / 100.0;
        }

        double precise(double value) {
            return std::round(value * 10000.0) / 10000.0;
        }

        std::string hex_colour(const std::array<uint8_t, 4>& colour) {
            return std::format("#{:02x}{:02x}{:02x}{:02x}", colour[0], colour[1], colour[2], colour[3]);
        }

        std::string column_type(int32_t keys, int32_t column, int32_t special_style) {
            if (keys % 2 == 1 && column == keys / 2) {
                return "s";
            }

            if (keys % 2 == 0 && keys > 4 && (special_style == 1 || special_style == 2)) {
                int32_t special = special_style == 1 ? 0 : keys - 1;

                if (column == special) {
                    return "s";
                }

                int32_t index = special_style == 1 ? column - 1 : column;
                return std::min(index, keys - 2 - index) % 2 == 0 ? "1" : "2";
            }

            return std::min(column, keys - 1 - column) % 2 == 0 ? "1" : "2";
        }

        int32_t osu_choice(const OsuValues& values, const std::string& key, int32_t fallback) {
            std::optional<std::string> value = osu_value(values, key);

            if (!value) {
                return fallback;
            }

            std::string word = lowercase(*value);
            return word == "left" ? 0 : word == "right" ? 1
                : word == "both"                        ? 2
                                                        : (int32_t)osu_number(values, key).value_or(fallback);
        }

        bool osu_flip(const OsuValues& values, const std::string& key, int32_t keys) {
            if (std::optional<double> all = osu_number(values, key)) {
                return *all != 0;
            }

            int32_t on = 0;
            int32_t off = 0;

            for (int32_t column = 0; column < keys; column++) {
                if (std::optional<double> one = osu_number(values, std::format("{}{}", key, column))) {
                    (*one != 0 ? on : off)++;
                }
            }

            return on >= off;
        }

        double light_fps(size_t frames) {
            return frames > 0 ? std::min(OSU_LIGHTING_MAX_FPS, frames * 1000.0 / OSU_LIGHTING_MS) : OSU_LIGHTING_MAX_FPS;
        }
    }

    SkinImportResult import_osu_skin(Archive& archive, const ArchiveEntries& entries, const std::string& root, const std::filesystem::path& archive_path, const std::filesystem::path& skins_directory, const std::filesystem::path& builtin_skins_directory) {
        SkinImportResult result;
        std::error_code ec;
        std::string prefix = root.empty() ? std::string() : root + "/";
        OsuFiles files;
        std::optional<std::string> ini_text;

        for (const auto& [name, index] : entries) {
            if (!name.starts_with(prefix)) {
                continue;
            }

            std::string relative = lowercase(name.substr(prefix.size()));
            size_t dot = relative.rfind('.');
            std::string stem = dot == std::string::npos ? relative : relative.substr(0, dot);
            std::string extension = dot == std::string::npos ? std::string() : relative.substr(dot);

            if (relative == "skin.ini") {
                ini_text = archive.read(index, MAX_SKIN_MANIFEST_BYTES);
            }
            else if (extension == ".png") {
                files.images[stem] = index;

                if (stem.ends_with("@2x")) {
                    files.doubled.insert(index);
                }
            }
            else if (extension == ".wav" || extension == ".ogg" || extension == ".mp3") {
                files.sounds[stem] = {index, extension};
            }
        }

        if (!ini_text) {
            result.error = "its skin.ini couldn't be read";
            return result;
        }

        OsuSkinIni ini = parse_osu_skin_ini(*ini_text);
        std::string skin_name = osu_value(ini.general, "name").value_or(root.empty() ? path_utf8(archive_path.stem()) : file_of(root));
        result.name = safe_skin_name(skin_name);

        if (result.name.empty()) {
            result.name = "osu! skin";
        }

        if (std::filesystem::exists(builtin_skins_directory / utf8_path(result.name), ec)) {
            result.name += " (osu!)";
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

        bool failed = false;

        auto write = [&](const std::filesystem::path& path, const Image& image) {
            std::error_code folder_ec;
            std::filesystem::create_directories(path.parent_path(), folder_ec);
            failed = failed || !write_png(path, image);
        };

        auto write_frames = [&](const std::vector<mz_uint>& frames, const std::filesystem::path& folder, const std::string& stem, auto transform) -> std::optional<Image> {
            std::optional<Image> first;

            for (size_t frame = 0; frame < frames.size(); frame++) {
                std::optional<Image> image = load_frame(archive, Frame{frames[frame]});

                if (!image) {
                    break;
                }

                Image converted = transform(std::move(*image));
                write(folder / (frames.size() > 1 ? std::format("{}-{}.png", stem, frame) : stem + ".png"), converted);

                if (!first) {
                    first = std::move(converted);
                }
            }

            return first;
        };

        auto aspect = [](const Image& image) {
            return image.width > 0 ? (double)image.height / image.width : 1.0;
        };

        auto unchanged = [](Image image) {
            return image;
        };

        auto stage_height = [&](const std::vector<mz_uint>& frames) -> std::optional<double> {
            std::optional<Image> image = frames.empty() ? std::nullopt : load_frame(archive, Frame{frames.front()});
            return image ? std::optional<double>(image->height / files.scale(frames.front()) / OSU_STAGE_HEIGHT) : std::nullopt;
        };

        static const OsuValues no_values;
        const OsuValues& first_section = ini.mania.empty() ? no_values : ini.mania.begin()->second;

        auto resolve = [&](const OsuValues& values, const std::string& key, const std::string& fallback) {
            std::vector<mz_uint> custom = files.frames(osu_image_name(osu_value(values, key).value_or(fallback)));
            return custom.empty() ? files.frames(fallback) : custom;
        };

        auto differs = [&](const OsuValues& values, const std::string& key) {
            return &values != &first_section && osu_value(values, key) != osu_value(first_section, key);
        };

        bool versioned = osu_value(ini.general, "version") && osu_number(ini.general, "version").value_or(OSU_BODY_STYLE_VERSION) >= OSU_BODY_STYLE_VERSION;
        bool stretched_bodies = !versioned;
        nlohmann::json frame_rates = {{"hold_body_held", precise(1000.0 / OSU_HOLD_BODY_FRAME_MS)}};

        struct SharedImage {
            const char* key;
            const char* fallback;
            const char* output;
            bool animated;
        };

        static constexpr std::array<SharedImage, 13> SHARED_IMAGES = {{
            {"hit300g", "mania-hit300g", "judgement_perfect", true},
            {"hit300", "mania-hit300", "judgement_amazing", true},
            {"hit200", "mania-hit200", "judgement_great", true},
            {"hit100", "mania-hit100", "judgement_good", true},
            {"hit50", "mania-hit50", "judgement_bad", true},
            {"hit0", "mania-hit0", "judgement_miss", true},
            {"stageleft", "mania-stage-left", "stage_left", false},
            {"stageright", "mania-stage-right", "stage_right", false},
            {"stagebottom", "mania-stage-bottom", "stage_bottom", true},
            {"lightingn", "lightingn", "hit_light", true},
            {"lightingl", "lightingl", "hold_light", true},
            {"warningarrow", "mania-warningarrow", "warning_arrow", false},
            {"", "comboburst-mania", "combo_burst", true},
        }};

        auto write_shared = [&](const OsuValues& values, const SharedImage& shared, const std::filesystem::path& folder) {
            std::vector<mz_uint> frames = std::string_view(shared.key).empty() ? files.frames(shared.fallback) : resolve(values, shared.key, shared.fallback);

            if (!frames.empty()) {
                write_frames(shared.animated ? frames : std::vector<mz_uint>{frames.front()}, folder, shared.output, unchanged);
            }

            return frames;
        };

        std::map<std::string, std::vector<mz_uint>> shared_frames;

        for (const SharedImage& shared : SHARED_IMAGES) {
            shared_frames[shared.output] = write_shared(first_section, shared, staging);

            if (std::string_view(shared.output).starts_with("judgement_")) {
                frame_rates[shared.output] = OSU_JUDGEMENT_FPS;
            }
        }

        std::string combo_prefix = osu_image_name(osu_value(ini.fonts, "comboprefix").value_or("score"));
        std::vector<mz_uint> digits;

        for (int32_t digit = 0; digit < 10; digit++) {
            if (std::optional<mz_uint> index = files.image(std::format("{}-{}", combo_prefix, digit))) {
                digits.push_back(*index);
            }
        }

        nlohmann::json root_playfield = {
            {"note_gradient", 0},
            {"note_opacity", 100},
            {"receptor_opacity", 100},
            {"snap_colors", 0},
            {"combo_break_min", OSU_COMBO_BREAK_MIN},
            {"combo_break_pop", 1},
            {"key_labels", 1},
            {"combo_burst_random", osu_number(ini.general, "comboburstrandom").value_or(0.0) != 0 ? 1 : 0},
            {"combo_scale", 1},
            {"combo_pop_from", OSU_COMBO_POP_FROM},
            {"combo_pop_to", 1},
            {"combo_pop_speed", OSU_COMBO_POP_SPEED},
            {"judgement_pop_from", 1},
            {"judgement_pop_to", OSU_JUDGEMENT_POP_TO},
            {"judgement_pop_speed", OSU_JUDGEMENT_POP_SPEED},
            {"judgement_fade_delay", OSU_JUDGEMENT_FADE_DELAY},
            {"judgement_fade_speed", OSU_JUDGEMENT_FADE_SPEED},
        };

        if (digits.size() == 10) {
            for (int32_t digit = 0; digit < 10; digit++) {
                write_frames({digits[(size_t)digit]}, staging, std::format("combo_digit-{}", digit), unchanged);
            }

            if (std::optional<Image> zero = load_frame(archive, Frame{digits.front()}); zero && zero->height > 0) {
                double height = zero->height / files.scale(digits.front());
                root_playfield["combo_digit_height"] = precise(height / OSU_STAGE_HEIGHT);
                root_playfield["combo_digit_overlap"] = precise(osu_number(ini.fonts, "combooverlap").value_or(0.0) / height);
            }
        }

        if (std::optional<double> height = stage_height(shared_frames["combo_burst"])) {
            root_playfield["combo_burst_height"] = precise(*height);
        }

        if (std::optional<double> height = stage_height(shared_frames["warning_arrow"])) {
            root_playfield["warning_arrow_height"] = precise(*height);
        }

        static constexpr std::array<std::pair<const char*, const char*>, 3> SOUNDS = {{
            {"normal-hitnormal", "hitsound"},
            {"combobreak", "combo_break"},
            {"comboburst", "combo_burst"},
        }};

        for (const auto& [name, output] : SOUNDS) {
            if (auto sound = files.sounds.find(name); sound != files.sounds.end()) {
                if (std::optional<std::string> data = archive.read(sound->second.first, MAX_SKIN_FILE_BYTES)) {
                    std::ofstream(staging / (output + sound->second.second), std::ios::binary | std::ios::trunc) << *data;
                }
            }
        }

        nlohmann::json keymodes = nlohmann::json::object();
        bool any_notes = false;

        for (int32_t keys = 1; keys <= OSU_MAX_KEYS; keys++) {
            auto section = ini.mania.find(keys);
            const OsuValues& values = section != ini.mania.end() ? section->second : no_values;
            std::filesystem::path folder = staging / std::format("{}k", keys);
            std::vector<double> widths = osu_numbers(osu_value(values, "columnwidth").value_or(""));
            std::vector<double> spacings = osu_numbers(osu_value(values, "columnspacing").value_or(""));
            std::vector<double> column_widths;
            double width_total = 0;
            double spacing_total = 0;

            for (int32_t column = 0; column < keys; column++) {
                double width = column < (int32_t)widths.size() && widths[column] > 0 ? widths[column] : OSU_DEFAULT_COLUMN_WIDTH;
                column_widths.push_back(width);
                width_total += width;
            }

            for (int32_t gap = 0; gap + 1 < keys; gap++) {
                spacing_total += gap < (int32_t)spacings.size() ? std::max(0.0, spacings[gap]) : 0.0;
            }

            double column_width = width_total / keys;
            double spacing = keys > 1 ? spacing_total / (keys - 1) : 0.0;
            double lane_width = (width_total + spacing_total) / keys;
            double height_basis = osu_number(values, "widthfornoteheightscale").value_or(0.0);
            height_basis = height_basis > 0 ? height_basis : *std::min_element(column_widths.begin(), column_widths.end());
            double note_height_scale = height_basis / column_width;
            double hit_position = std::clamp(osu_number(values, "hitposition").value_or(OSU_DEFAULT_HIT_POSITION), OSU_MIN_HIT_POSITION, OSU_CANVAS_HEIGHT);
            int32_t special_style = (int32_t)osu_number(values, "specialstyle").value_or(0.0);
            std::vector<mz_uint> light = resolve(values, "stagelight", "mania-stage-light");
            std::optional<double> note_aspect;
            std::optional<double> body_aspect;
            std::optional<double> cap_aspect;
            std::optional<double> light_height;
            std::optional<double> receptor_height;

            for (int32_t column = 0; column < keys; column++) {
                std::string type = column_type(keys, column, special_style);
                std::string lane = std::to_string(column + 1);

                auto pick = [&](const std::string& key, const std::string& fallback) {
                    return resolve(values, key, fallback);
                };

                std::vector<mz_uint> note = pick(std::format("noteimage{}", column), "mania-note" + type);
                std::vector<mz_uint> head = pick(std::format("noteimage{}h", column), "mania-note" + type + "h");
                std::vector<mz_uint> body = pick(std::format("noteimage{}l", column), "mania-note" + type + "l");
                std::vector<mz_uint> tail = pick(std::format("noteimage{}t", column), "mania-note" + type + "t");
                std::vector<mz_uint> key = pick(std::format("keyimage{}", column), "mania-key" + type);
                std::vector<mz_uint> pressed = pick(std::format("keyimage{}d", column), "mania-key" + type + "d");

                if (tail.empty()) {
                    tail = head.empty() ? note : head;
                }

                if (std::optional<Image> image = write_frames(note, folder, "note_" + lane, unchanged)) {
                    note_aspect = note_aspect.value_or(aspect(*image));
                    any_notes = true;
                }

                write_frames(head, folder, "hold_head_" + lane, unchanged);

                if (std::optional<Image> image = body.empty() ? std::nullopt : write_frames({body.front()}, folder, "hold_body_" + lane, unchanged)) {
                    body_aspect = body_aspect.value_or(aspect(*image));
                }

                if (body.size() > 1) {
                    write_frames(body, folder, "hold_body_held_" + lane, unchanged);
                }

                if (std::optional<Image> image = write_frames(tail, folder, "hold_cap_" + lane, unchanged)) {
                    cap_aspect = cap_aspect.value_or(aspect(*image));
                }

                if (std::optional<Image> image = key.empty() ? std::nullopt : write_frames({key.front()}, folder, "receptor_" + lane, unchanged)) {
                    receptor_height = receptor_height.value_or(image->height / files.scale(key.front()) / OSU_STAGE_HEIGHT);
                }

                if (!pressed.empty()) {
                    write_frames({pressed.front()}, folder, "receptor_pressed_" + lane, unchanged);
                }

                if (!light.empty()) {
                    std::array<uint8_t, 4> colour = osu_colour(values, std::format("colourlight{}", column + 1)).value_or(OSU_WHITE);
                    std::optional<Image> image = write_frames(light, folder, "lane_light_" + lane, [&](Image frame) {
                        return tinted(std::move(frame), colour);
                    });

                    if (image) {
                        light_height = light_height.value_or(image->height / files.scale(light.front()) / OSU_STAGE_HEIGHT);
                    }
                }

                if (std::optional<std::array<uint8_t, 4>> colour = osu_colour(values, std::format("colour{}", column + 1))) {
                    write(folder / ("lane_background_" + lane + ".png"), solid(4, 4, *colour));
                }
            }

            std::vector<double> lines = osu_numbers(osu_value(values, "columnlinewidth").value_or(""));
            double line_total = 0;

            for (int32_t line = 1; line < keys; line++) {
                line_total += line < (int32_t)lines.size() ? lines[line] : OSU_DEFAULT_COLUMN_LINE_WIDTH;
            }

            double line_width = keys > 1 ? line_total / (keys - 1) : 0.0;

            if (line_width > 0) {
                int32_t height = std::max(1, (int32_t)std::lround(OSU_CANVAS_HEIGHT / line_width));
                write(folder / "lane_separator.png", solid(1, height, osu_colour(values, "colourcolumnline").value_or(OSU_WHITE)));
            }

            for (const SharedImage& shared : SHARED_IMAGES) {
                if (!std::string_view(shared.key).empty() && differs(values, shared.key)) {
                    shared_frames[std::format("{}/{}", keys, shared.output)] = write_shared(values, shared, folder);
                }
            }

            auto effective = [&](const std::string& output) -> const std::vector<mz_uint>& {
                auto local = shared_frames.find(std::format("{}/{}", keys, output));
                return local != shared_frames.end() ? local->second : shared_frames[output];
            };

            double note_height = column_width * note_height_scale * note_aspect.value_or(1.0);
            double judgement_line = rounded(std::clamp((OSU_CANVAS_HEIGHT - hit_position - column_width / 2 + note_height / 2) / OSU_CANVAS_HEIGHT * 100.0, 0.0, 100.0));
            double light_position = osu_number(values, "lightposition").value_or(OSU_DEFAULT_LIGHT_POSITION);
            nlohmann::json playfield = {
                {"receptors_over_notes", osu_number(values, "keysundernotes").value_or(0.0) == 1.0 ? 0 : 1},
                {"hit_line_offset", precise(-note_height / 2 / OSU_CANVAS_HEIGHT)},
                {"lane_light_offset", precise((OSU_CANVAS_HEIGHT - light_position) / OSU_CANVAS_HEIGHT - judgement_line / 100.0)},
                {"note_height_scale", precise(note_height_scale)},
                {"upscroll", osu_number(values, "upsidedown").value_or(0.0) != 0 ? 1 : 0},
                {"upscroll_flip_notes", osu_flip(values, "noteflipwhenupsidedown", keys) ? 1 : 0},
                {"upscroll_flip_receptors", osu_flip(values, "keyflipwhenupsidedown", keys) ? 1 : 0},
                {"barline_height", precise(std::max(0.0, osu_number(values, "barlineheight").value_or(OSU_DEFAULT_BARLINE_HEIGHT)) / OSU_STAGE_HEIGHT)},
                {"combo_burst_side", osu_choice(values, "comboburststyle", 1)},
            };

            if (receptor_height) {
                playfield["receptor_height"] = precise(*receptor_height);
            }

            bool judgement_line_shown = osu_number(values, "judgementline").value_or(1.0) != 0;
            std::array<uint8_t, 4> line_colour = osu_colour(values, "colourjudgementline").value_or(OSU_WHITE);
            line_colour[3] = OSU_JUDGEMENT_LINE_ALPHA;
            std::vector<mz_uint> hint = resolve(values, "stagehint", "mania-stage-hint");

            if (!hint.empty()) {
                std::optional<Image> image = write_frames({hint.front()}, folder, "judgement_line", [&](Image frame) {
                    return judgement_line_shown ? with_line(std::move(frame), line_colour) : frame;
                });

                if (image) {
                    playfield["judgement_line_image_height"] = precise(image->height / files.scale(hint.front()) * OSU_STAGE_HINT_SCALE / OSU_STAGE_HEIGHT);
                }
            }
            else if (judgement_line_shown) {
                write(folder / "judgement_line.png", solid(4, 1, line_colour));
                playfield["judgement_line_image_height"] = precise(1.0 / OSU_STAGE_HEIGHT);
            }

            if (light_height) {
                playfield["lane_light_height"] = rounded(std::clamp(*light_height, 0.05, 1.0));
            }

            auto lighting_scale = [&](const char* widths_key) {
                if (!versioned) {
                    return 1.0;
                }

                std::vector<double> light_widths = osu_numbers(osu_value(values, widths_key).value_or(""));
                double total = 0;

                for (int32_t column = 0; column < keys; column++) {
                    double width = column < (int32_t)light_widths.size() && light_widths[column] > 0 ? light_widths[column] : column_widths[(size_t)column];
                    total += width / OSU_DEFAULT_COLUMN_WIDTH;
                }

                return total / keys;
            };

            nlohmann::json mode_rates = {
                {"lane_light", (double)std::max(1, (int32_t)osu_number(values, "lightframepersecond").value_or(OSU_DEFAULT_LIGHT_FPS))},
                {"hit_light", precise(light_fps(effective("hit_light").size()))},
                {"hold_light", precise(light_fps(effective("hold_light").size()))},
            };

            if (std::optional<double> height = stage_height(effective("hit_light"))) {
                playfield["hit_light_height"] = precise(*height * lighting_scale("lightingnwidth"));
            }

            if (std::optional<double> height = stage_height(effective("hold_light"))) {
                playfield["hold_light_height"] = precise(*height * lighting_scale("lightinglwidth"));
            }

            if (std::optional<double> height = stage_height(effective("stage_bottom"))) {
                playfield["stage_bottom_height"] = precise(*height * OSU_STAGE_BOTTOM_SCALE);
            }

            if (!effective("judgement_amazing").empty()) {
                playfield["judgement_scale"] = precise(OSU_REFERENCE_HEIGHT / OSU_STAGE_HEIGHT / files.scale(effective("judgement_amazing").front()));
            }

            nlohmann::json hud = {
                {"lane_width", rounded(lane_width / OSU_CANVAS_HEIGHT * 100.0)},
                {"judgement_line_height", judgement_line},
            };

            if (std::optional<double> score = osu_number(values, "scoreposition")) {
                hud["judgement_height"] = rounded(std::clamp((OSU_CANVAS_HEIGHT - *score) / OSU_CANVAS_HEIGHT * 100.0, 0.0, 100.0));
            }

            if (std::optional<double> combo = osu_number(values, "comboposition")) {
                hud["combo_height"] = rounded(std::clamp((OSU_CANVAS_HEIGHT - *combo) / OSU_CANVAS_HEIGHT * 100.0, 0.0, 100.0));
            }

            nlohmann::json layout = {{"hud", hud}, {"playfield", playfield}};

            std::optional<double> body_style = osu_number(values, "notebodystyle");
            bool stretched = body_style ? *body_style == 0 : stretched_bodies;
            nlohmann::json mode = {{"layout", layout}, {"hold_style", stretched ? "stretch" : "tile"}, {"key_spacing", precise(spacing / column_width)}, {"frame_rates", mode_rates}};

            if (body_aspect) {
                mode["hold_body_aspect"] = rounded(*body_aspect);
            }

            if (cap_aspect) {
                mode["hold_body_tail_offset"] = -rounded(note_aspect.value_or(1.0) * note_height_scale / 2);
            }

            keymodes[std::format("{}k", keys)] = mode;
        }

        if (!any_notes || failed) {
            std::filesystem::remove_all(staging, ec);
            result.error = failed ? "could not write the converted images" : "it has no osu!mania note images";
            return result;
        }

        nlohmann::json colors = {
            {"barline", hex_colour(osu_colour(first_section, "colourbarline").value_or(OSU_WHITE))},
            {"combo_holding", hex_colour(osu_colour(first_section, "colourhold").value_or(OSU_DEFAULT_HOLD_COLOUR))},
            {"combo_break", hex_colour(osu_colour(first_section, "colourbreak").value_or(OSU_DEFAULT_BREAK_COLOUR))},
            {"key_label", hex_colour(osu_colour(first_section, "colourkeywarning").value_or(OSU_DEFAULT_KEY_WARNING_COLOUR))},
        };

        nlohmann::json config = {
            {"format", 1},
            {"name", result.name},
            {"source", "osu"},
            {"inherits", "default"},
            {"tint", "none"},
            {"note_shape", "image"},
            {"receptor_style", "column"},
            {"key_spacing", 0},
            {"receptor_alpha", 255},
            {"receptor_pressed_alpha", 255},
            {"frame_rate", OSU_DEFAULT_FPS},
            {"frame_rates", frame_rates},
            {"colors", colors},
            {"layout", {{"playfield", root_playfield}}},
            {"keymodes", keymodes},
        };

        if (std::optional<std::string> author = osu_value(ini.general, "author")) {
            config["author"] = *author;
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
