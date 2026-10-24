#include "Skin.h"
#include "gse/Game.h"
#include "Const.h"
#include "gse/Platform.h"
#include "SettingsManager.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "Utf8Path.h"
#include "gse/render/SpriteDecal.h"
#include "gse/render/Color.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <numbers>
#include <cctype>
#include <format>
#include <fstream>
#include <iostream>
#include <set>
#include <string_view>

namespace nascent {
    namespace {
        struct ImageSlot {
            const char* stem;
            std::optional<SkinImage> fallback;
        };

        constexpr std::array<ImageSlot, SKIN_IMAGE_COUNT> IMAGE_SLOTS = {{
            {"note", std::nullopt},
            {"receptor", SkinImage::NOTE},
            {"receptor_pressed", SkinImage::RECEPTOR},
            {"hold_head", SkinImage::NOTE},
            {"hold_body", std::nullopt},
            {"hold_cap", std::nullopt},
            {"mascot", std::nullopt},
            {"judgement_perfect", std::nullopt},
            {"judgement_amazing", std::nullopt},
            {"judgement_great", std::nullopt},
            {"judgement_good", std::nullopt},
            {"judgement_bad", std::nullopt},
            {"judgement_miss", std::nullopt},
            {"play_background", std::nullopt},
            {"menu_background", std::nullopt},
            {"lane_background", std::nullopt},
            {"lane_light", std::nullopt},
            {"stage_left", std::nullopt},
            {"stage_right", std::nullopt},
            {"lane_separator", std::nullopt},
            {"judgement_line", std::nullopt},
            {"lane_cover", std::nullopt},
            {"hold_head_held", SkinImage::HOLD_HEAD},
            {"hold_body_held", SkinImage::HOLD_BODY},
            {"grade_x", std::nullopt},
            {"grade_s", std::nullopt},
            {"grade_a", std::nullopt},
            {"grade_b", std::nullopt},
            {"grade_c", std::nullopt},
            {"grade_d", std::nullopt},
            {"grade_e", std::nullopt},
            {"grade_f", std::nullopt},
            {"progress_track", std::nullopt},
            {"progress_fill", std::nullopt},
            {"timing_bar_background", std::nullopt},
            {"timing_bar_hit", std::nullopt},
            {"timing_bar_average", std::nullopt},
            {"hit_light", std::nullopt},
            {"hold_light", std::nullopt},
            {"stage_bottom", std::nullopt},
            {"combo_burst", std::nullopt},
            {"combo_digit", std::nullopt},
            {"warning_arrow", std::nullopt},
        }};

        constexpr std::array<SkinImage, 2> SNAP_IMAGES = {SkinImage::NOTE, SkinImage::HOLD_HEAD};

        constexpr std::array<const char*, SKIN_SOUND_COUNT> SOUND_STEMS = {
            "hitsound",
            "idle_loop",
            "account_loop",
            "judgement_perfect",
            "judgement_amazing",
            "judgement_great",
            "judgement_good",
            "judgement_bad",
            "judgement_miss",
            "combo_break",
            "combo_burst",
        };

        constexpr std::array<const char*, 4> SOUND_EXTENSIONS = {".mp3", ".wav", ".ogg", ".flac"};
        constexpr std::array<std::string_view, 12> GENERAL_KEYS = {"format", "name", "author", "version", "source", "inherits", "hidden", "colors", "difficulty_scale", "fonts", "layout", "keymodes"};
        constexpr std::array<std::string_view, 17> PLAYFIELD_KEYS = {
            "tint",
            "tint_notes",
            "hold_style",
            "note_shape",
            "receptor_style",
            "hold_alpha",
            "hold_body_aspect",
            "hold_body_head_offset",
            "hold_body_tail_offset",
            "key_spacing",
            "note_alpha",
            "receptor_alpha",
            "receptor_pressed_alpha",
            "lane_images",
            "mascot",
            "frame_rate",
            "frame_rates",
        };
        constexpr std::string_view MENU_PLUCK_PREFIX = "menu_pluck_";
        constexpr double MAX_FRAME_RATE = 240.0;
        constexpr double MIN_FONT_SIZE = 4.0;
        constexpr double MAX_FONT_SIZE = 400.0;

        template <size_t N>
        bool listed(const std::array<std::string_view, N>& keys, std::string_view key) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        }

        nlohmann::json read_manifest(const std::filesystem::path& directory, std::string* error = nullptr) {
            std::ifstream file(directory / Skin::MANIFEST_FILE);

            if (!file) {
                return nlohmann::json::object();
            }

            try {
                nlohmann::json config = nlohmann::json::parse(file);

                if (config.is_object()) {
                    return config;
                }

                if (error != nullptr) {
                    *error = "skin.json must be a JSON object, like { \"name\": \"My skin\" }";
                }
            }
            catch (const nlohmann::json::parse_error& e) {
                if (error != nullptr) {
                    std::string_view message = e.what();
                    size_t bracket = message.find("] ");
                    *error = std::format("skin.json isn't valid JSON: {}", bracket == std::string_view::npos ? message : message.substr(bracket + 2));
                }
            }

            return nlohmann::json::object();
        }

        bool is_valid_id(const std::string& id) {
            return !id.empty() && id != "." && id != ".." && id.find_first_of("/\\:") == std::string::npos;
        }

        std::string lowercase(std::string text) {
            for (char& c : text) {
                c = (char)std::tolower((unsigned char)c);
            }

            return text;
        }

        uint8_t clamp_alpha(const nlohmann::json& value) {
            return (uint8_t)std::clamp(value.get<double>(), 0.0, 255.0);
        }

        std::optional<olc::Pixel> parse_color(std::string_view text) {
            if (!text.starts_with('#') || (text.size() != 7 && text.size() != 9)) {
                return std::nullopt;
            }

            uint8_t channels[4] = {0, 0, 0, 255};

            for (size_t index = 0; index * 2 + 1 < text.size(); index++) {
                std::string_view digits = text.substr(1 + index * 2, 2);
                auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), channels[index], 16);

                if (error != std::errc() || end != digits.data() + digits.size()) {
                    return std::nullopt;
                }
            }

            return olc::Pixel(channels[0], channels[1], channels[2], channels[3]);
        }

        std::optional<uint8_t> parse_key_count(std::string_view text) {
            if (!text.empty() && (text.back() == 'k' || text.back() == 'K')) {
                text.remove_suffix(1);
            }

            int value = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

            if (text.empty() || error != std::errc() || end != text.data() + text.size() || value < 1 || value > MAX_LANES) {
                return std::nullopt;
            }

            return (uint8_t)value;
        }

        std::optional<uint8_t> keymode_folder(std::string_view name) {
            return name.ends_with('k') ? parse_key_count(name) : std::nullopt;
        }

        std::string_view without_number_suffix(std::string_view text, char separator) {
            size_t split = text.rfind(separator);

            if (split == std::string_view::npos || split + 1 == text.size()) {
                return text;
            }

            std::string_view digits = text.substr(split + 1);
            return std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }) ? text.substr(0, split) : text;
        }

        std::string snap_stem(SkinImage image, size_t snap) {
            return std::format("{}_{}", IMAGE_SLOTS[(size_t)image].stem, NOTE_SNAP_NAMES[snap]);
        }

        bool is_image_stem(std::string_view stem) {
            if (stem == "title_gradient") {
                return true;
            }

            for (SkinImage image : SNAP_IMAGES) {
                for (size_t snap = 0; snap < NOTE_SNAP_COUNT; snap++) {
                    if (stem == snap_stem(image, snap)) {
                        return true;
                    }
                }
            }

            return std::any_of(IMAGE_SLOTS.begin(), IMAGE_SLOTS.end(), [&](const ImageSlot& slot) {
                return stem == slot.stem;
            });
        }

        bool tiled_body(SkinImage image) {
            return image == SkinImage::HOLD_BODY || image == SkinImage::HOLD_BODY_HELD;
        }

        double seconds_since(std::chrono::steady_clock::time_point start) {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }

        std::optional<SkinImage> judgement_slot(HitScore score) {
            switch (score) {
                case HitScore::PERFECT:
                    return SkinImage::JUDGEMENT_PERFECT;
                case HitScore::AMAZING:
                    return SkinImage::JUDGEMENT_AMAZING;
                case HitScore::GREAT:
                    return SkinImage::JUDGEMENT_GREAT;
                case HitScore::GOOD:
                    return SkinImage::JUDGEMENT_GOOD;
                case HitScore::BAD:
                    return SkinImage::JUDGEMENT_BAD;
                case HitScore::MISS:
                    return SkinImage::JUDGEMENT_MISS;
                default:
                    return std::nullopt;
            }
        }
    }

    void SkinManifest::problem(std::string message) {
        problems.push_back(std::move(message));
    }

    void SkinManifest::apply_colors(const nlohmann::json& config) {
        if (!config.is_object()) {
            return;
        }

        apply_difficulty_scale(config);
        auto entries = config.find("colors");

        if (entries == config.end()) {
            return;
        }

        if (!entries->is_object()) {
            problem("\"colors\" should be an object of color names, like { \"text\": \"#ffffff\" }");
            return;
        }

        for (const auto& [key, value] : entries->items()) {
            if (key.starts_with('_')) {
                continue;
            }

            auto slot = std::find_if(SKIN_COLOR_NAMES.begin(), SKIN_COLOR_NAMES.end(), [&](const char* name) {
                return key == name;
            });

            if (slot == SKIN_COLOR_NAMES.end()) {
                problem(std::format("\"{}\" isn't a color name", key));
                continue;
            }

            std::optional<olc::Pixel> color = value.is_string() ? parse_color(value.get<std::string>()) : std::nullopt;

            if (!color) {
                problem(std::format("color \"{}\" must look like #rrggbb or #rrggbbaa", key));
                continue;
            }

            colors[(size_t)(slot - SKIN_COLOR_NAMES.begin())] = color;
        }
    }

    void SkinManifest::apply_difficulty_scale(const nlohmann::json& config) {
        auto scale = config.find("difficulty_scale");

        if (scale == config.end()) {
            return;
        }

        if (!scale->is_array()) {
            problem("\"difficulty_scale\" should be a list like [[0, \"#6fa8ff\"], [100, \"#f2eaff\"]]");
            return;
        }

        std::vector<std::pair<double, olc::Pixel>> stops;

        for (const nlohmann::json& stop : *scale) {
            std::optional<olc::Pixel> color = stop.is_array() && stop.size() == 2 && stop[0].is_number() && stop[1].is_string() ? parse_color(stop[1].get<std::string>()) : std::nullopt;

            if (!color) {
                problem("difficulty_scale entries must look like [difficulty, \"#rrggbb\"]");
                return;
            }

            stops.emplace_back(stop[0].get<double>(), *color);
        }

        std::sort(stops.begin(), stops.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        difficulty_scale = std::move(stops);
    }

    void SkinManifest::apply_fonts(const nlohmann::json& config) {
        auto entries = config.find("fonts");

        if (entries == config.end()) {
            return;
        }

        if (!entries->is_object()) {
            problem("\"fonts\" should be an object, like { \"large\": { \"file\": \"title.ttf\", \"size\": 72 } }");
            return;
        }

        for (const auto& [role, value] : entries->items()) {
            if (role.starts_with('_')) {
                continue;
            }

            auto slot = std::find_if(SKIN_FONT_NAMES.begin(), SKIN_FONT_NAMES.end(), [&](const char* name) {
                return role == name;
            });

            if (slot == SKIN_FONT_NAMES.end()) {
                problem(std::format("\"{}\" isn't a font; the fonts are large, medium, small, judgement and combo", role));
                continue;
            }

            if (!value.is_object()) {
                problem(std::format("font \"{}\" should look like {{ \"file\": \"font.ttf\", \"size\": 36 }}", role));
                continue;
            }

            SkinFontSpec& spec = fonts[(size_t)(slot - SKIN_FONT_NAMES.begin())];

            for (const auto& [key, setting] : value.items()) {
                if (key == "file" && setting.is_string()) {
                    spec.file = setting.get<std::string>();
                }
                else if (key == "size" && setting.is_number()) {
                    spec.size = std::clamp(setting.get<double>(), MIN_FONT_SIZE, MAX_FONT_SIZE);
                }
                else if (!key.starts_with('_')) {
                    problem(std::format("font \"{}\" has an unknown or invalid \"{}\"; use \"file\" (text) and \"size\" (number)", role, key));
                }
            }
        }
    }

    void SkinManifest::apply_layout(const nlohmann::json& config, const std::string& prefix) {
        for (const auto& [key, value] : config.items()) {
            if (key.starts_with('_')) {
                continue;
            }

            std::string name = prefix.empty() ? key : prefix + "." + key;

            if (value.is_object()) {
                apply_layout(value, name);
            }
            else if (value.is_number()) {
                layout[name] = value.get<double>();
            }
            else if (value.is_boolean()) {
                layout[name] = value.get<bool>() ? 1.0 : 0.0;
            }
            else {
                problem(std::format("layout value \"{}\" should be a number", name));
            }
        }
    }

    void SkinManifest::apply_playfield(const nlohmann::json& config, const std::string& context) {
        auto setting = [&](const char* key, bool (nlohmann::json::*check)() const noexcept, const char* expected) -> const nlohmann::json* {
            auto it = config.find(key);

            if (it == config.end()) {
                return nullptr;
            }

            if (!((*it).*check)()) {
                problem(std::format("{}\"{}\" should be {}", context, key, expected));
                return nullptr;
            }

            return &*it;
        };

        auto number = [&](const char* key) {
            return setting(key, &nlohmann::json::is_number, "a number");
        };

        if (const nlohmann::json* value = setting("tint", &nlohmann::json::is_string, "\"gradient\" or \"none\"")) {
            std::string style = value->get<std::string>();
            tint = style == "none" ? SkinTint::NONE : SkinTint::GRADIENT;

            if (style != "none" && style != "gradient") {
                problem(std::format("{}\"tint\" should be \"gradient\" or \"none\"", context));
            }
        }
        else if (const nlohmann::json* legacy = setting("tint_notes", &nlohmann::json::is_boolean, "true or false")) {
            tint = legacy->get<bool>() ? SkinTint::GRADIENT : SkinTint::NONE;
        }

        if (const nlohmann::json* value = setting("hold_style", &nlohmann::json::is_string, "\"repeat\", \"stretch\" or \"tile\"")) {
            std::string style = value->get<std::string>();
            hold_style = style == "stretch" ? HoldStyle::STRETCH : style == "tile" ? HoldStyle::TILE
                                                                                   : HoldStyle::REPEAT;

            if (style != "stretch" && style != "tile" && style != "repeat") {
                problem(std::format("{}\"hold_style\" should be \"repeat\", \"stretch\" or \"tile\"", context));
            }
        }

        if (const nlohmann::json* value = setting("note_shape", &nlohmann::json::is_string, "\"square\" or \"image\"")) {
            std::string shape = value->get<std::string>();
            note_shape = shape == "image" ? NoteShape::IMAGE : NoteShape::SQUARE;

            if (shape != "image" && shape != "square") {
                problem(std::format("{}\"note_shape\" should be \"square\" or \"image\"", context));
            }
        }

        if (const nlohmann::json* value = setting("receptor_style", &nlohmann::json::is_string, "\"square\" or \"column\"")) {
            std::string style = value->get<std::string>();
            receptor_style = style == "column" ? ReceptorStyle::COLUMN : ReceptorStyle::SQUARE;

            if (style != "column" && style != "square") {
                problem(std::format("{}\"receptor_style\" should be \"square\" or \"column\"", context));
            }
        }

        if (const nlohmann::json* value = number("hold_body_aspect")) {
            hold_body_aspect = std::clamp(value->get<double>(), 0.001, 1000.0);
        }

        if (const nlohmann::json* value = number("hold_body_head_offset")) {
            hold_body_head_offset = std::clamp(value->get<double>(), -4.0, 4.0);
        }

        if (const nlohmann::json* value = number("hold_body_tail_offset")) {
            hold_body_tail_offset = std::clamp(value->get<double>(), -4.0, 4.0);
        }

        if (const nlohmann::json* value = number("key_spacing")) {
            key_spacing = std::clamp(value->get<double>(), 0.0, 4.0);
        }

        if (const nlohmann::json* value = number("hold_alpha")) {
            hold_alpha = clamp_alpha(*value);
        }

        if (const nlohmann::json* value = number("note_alpha")) {
            note_alpha = clamp_alpha(*value);
        }

        if (const nlohmann::json* value = number("receptor_alpha")) {
            receptor_alpha = clamp_alpha(*value);
        }

        if (const nlohmann::json* value = number("receptor_pressed_alpha")) {
            receptor_pressed_alpha = clamp_alpha(*value);
        }

        if (const nlohmann::json* value = number("frame_rate")) {
            frame_rate = std::clamp(value->get<double>(), 0.0, MAX_FRAME_RATE);
        }

        if (const nlohmann::json* value = setting("frame_rates", &nlohmann::json::is_object, "an object of image names, like { \"mascot\": 8 }")) {
            for (const auto& [image, rate] : value->items()) {
                if (image.starts_with('_')) {
                    continue;
                }

                if (!is_image_stem(image) || !rate.is_number()) {
                    problem(std::format("{}frame_rates \"{}\" should be an image name with a number", context, image));
                    continue;
                }

                frame_rates[image] = std::clamp(rate.get<double>(), 0.0, MAX_FRAME_RATE);
            }
        }

        if (const nlohmann::json* value = setting("mascot", &nlohmann::json::is_object, "an object with width, x and y")) {
            std::map<std::string_view, double*> targets = {{"width", &mascot.width}, {"x", &mascot.x}, {"y", &mascot.y}};

            for (const auto& [key, amount] : value->items()) {
                auto found = targets.find(key);
                double* target = found != targets.end() ? found->second : nullptr;

                if (target != nullptr && amount.is_number()) {
                    *target = std::clamp(amount.get<double>(), 0.0, 100.0);
                }
                else if (!key.starts_with('_')) {
                    problem(std::format("{}mascot \"{}\" should be width, x or y with a number", context, key));
                }
            }
        }

        if (const nlohmann::json* value = setting("lane_images", &nlohmann::json::is_object, "an object of key counts, like { \"4\": [1, 2, 2, 1] }")) {
            for (const auto& [keys, lanes] : value->items()) {
                std::optional<uint8_t> key_count = parse_key_count(keys);

                if (!key_count || !lanes.is_array()) {
                    problem(std::format("{}lane_images \"{}\" should be a key count with a list of image numbers", context, keys));
                    continue;
                }

                std::vector<uint8_t> mapping;

                for (const nlohmann::json& lane : lanes) {
                    mapping.push_back(lane.is_number() ? (uint8_t)std::clamp(lane.get<int>(), 1, (int)MAX_LANES) : (uint8_t)1);
                }

                lane_images[*key_count] = std::move(mapping);
            }
        }
    }

    void SkinManifest::apply(const nlohmann::json& config) {
        if (!config.is_object()) {
            return;
        }

        for (const auto& [key, value] : config.items()) {
            if (!key.starts_with('_') && !listed(GENERAL_KEYS, key) && !listed(PLAYFIELD_KEYS, key)) {
                problem(std::format("\"{}\" isn't a skin setting", key));
            }
        }

        if (config.contains("name")) {
            if (config["name"].is_string()) {
                name = config["name"].get<std::string>();
            }
            else {
                problem("\"name\" should be text");
            }
        }

        if (config.contains("inherits")) {
            inherits = config["inherits"].is_string() ? config["inherits"].get<std::string>() : std::string();
        }

        if (config.contains("hidden") && config["hidden"].is_boolean()) {
            hidden = config["hidden"].get<bool>();
        }

        apply_playfield(config, "");
        apply_colors(config);
        apply_fonts(config);

        if (config.contains("layout")) {
            if (config["layout"].is_object()) {
                apply_layout(config["layout"], "");
            }
            else {
                problem("\"layout\" should be an object, like { \"results\": { \"graph_height\": 0.32 } }");
            }
        }

        if (config.contains("keymodes")) {
            if (!config["keymodes"].is_object()) {
                problem("\"keymodes\" should be an object of key counts, like { \"7\": { \"key_spacing\": 0.1 } }");
                return;
            }

            for (const auto& [keys, overrides] : config["keymodes"].items()) {
                std::optional<uint8_t> key_count = parse_key_count(keys);

                if (!key_count || !overrides.is_object()) {
                    problem(std::format("keymodes \"{}\" should be a key count with an object of settings", keys));
                    continue;
                }

                for (const auto& [key, value] : overrides.items()) {
                    if (key == "layout") {
                        if (!value.is_object()) {
                            problem(std::format("keymodes {}: \"layout\" should be an object, like {{ \"playfield\": {{ \"judgement_scale\": 1.2 }} }}", keys));
                        }

                        continue;
                    }

                    if (!key.starts_with('_') && !listed(PLAYFIELD_KEYS, key)) {
                        problem(std::format("keymodes {}: \"{}\" can't be changed per key count", keys, key));
                    }
                }

                nlohmann::json& merged = keymodes[*key_count];

                if (!merged.is_object()) {
                    merged = nlohmann::json::object();
                }

                merged.update(overrides);
            }
        }
    }

    static std::atomic<uint32_t> skin_list_version = 0;

    uint32_t Skin::list_version() {
        return skin_list_version.load();
    }

    void Skin::list_changed() {
        skin_list_version++;
    }

    Skin::Skin(Game* game, const std::string& id) : game(game) {
        reload(id);
    }

    Skin::~Skin() {
        unload();
    }

    std::filesystem::path Skin::builtin_skins_directory() {
        return "assets/skins";
    }

    std::filesystem::path Skin::user_skins_directory() {
        return user_config_directory(SettingsManager::APP_NAME) / "skins";
    }

    std::filesystem::path Skin::skin_directory(const std::string& id) {
        if (!is_valid_id(id)) {
            return {};
        }

        std::error_code ec;

        for (const std::filesystem::path& root : {builtin_skins_directory(), user_skins_directory()}) {
            std::filesystem::path directory = root / utf8_path(id);

            if (std::filesystem::is_directory(directory, ec)) {
                return directory;
            }
        }

        return {};
    }

    std::vector<SkinInfo> Skin::available_skins() {
        std::vector<SkinInfo> skins;
        std::set<std::string> seen;
        std::error_code ec;

        for (const std::filesystem::path& root : {builtin_skins_directory(), user_skins_directory()}) {
            for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
                std::string id = path_utf8(entry.path().filename());

                if (!entry.is_directory(ec) || !is_valid_id(id) || entry.path().extension() == ".part" || !seen.insert(id).second) {
                    continue;
                }

                SkinManifest manifest;
                manifest.apply(read_manifest(entry.path()));

                if (!manifest.hidden) {
                    skins.push_back({id, manifest.name.empty() ? id : manifest.name});
                }
            }
        }

        std::sort(skins.begin(), skins.end(), [](const SkinInfo& a, const SkinInfo& b) {
            if ((a.id == DEFAULT_SKIN) != (b.id == DEFAULT_SKIN)) {
                return a.id == DEFAULT_SKIN;
            }

            return lowercase(a.name) < lowercase(b.name);
        });

        return skins;
    }

    void Skin::reload(const std::string& id, std::filesystem::path overlay) {
        unload();
        this->id = id;
        this->overlay = std::move(overlay);
        load();
        sync_logo();
    }

    void Skin::set_logo_skin(Skin* logo) {
        logo_skin = logo;
        sync_logo();
    }

    void Skin::sync_logo() {
        if (logo_skin == nullptr) {
            return;
        }

        std::filesystem::path folder;
        std::error_code ec;

        for (const std::filesystem::path& directory : chain) {
            if (std::filesystem::is_directory(directory / LOGO_FOLDER, ec)) {
                folder = directory / LOGO_FOLDER;
                break;
            }
        }

        logo_skin->reload(LOGO_SKIN, folder);

        if (!folder.empty()) {
            for (const std::string& problem : logo_skin->problems()) {
                load_problems.push_back(std::format("logo: {}", problem));
            }
        }
    }

    void Skin::load() {
        loaded_at = std::chrono::steady_clock::now();
        load_problems.clear();
        clear_lookups();
        keymode_manifests.clear();
        missing_layout.clear();

        if (skin_directory(id).empty()) {
            load_problems.push_back(std::format("the skin \"{}\" wasn't found, so the default skin is used", id));
            id = DEFAULT_SKIN;
        }

        std::vector<nlohmann::json> configs;
        std::vector<std::filesystem::path> config_directories;
        std::vector<std::string> read_errors;
        std::error_code ec;

        auto add = [&](const std::filesystem::path& directory) {
            std::string error;
            chain.push_back(directory);
            configs.push_back(read_manifest(directory, &error));
            config_directories.push_back(directory);
            read_errors.push_back(std::move(error));
        };

        if (!overlay.empty() && std::filesystem::is_directory(overlay, ec)) {
            add(overlay);
        }

        std::string current = id;

        while (!current.empty() && chain.size() < MAX_INHERITANCE_DEPTH) {
            std::filesystem::path directory = skin_directory(current);

            if (directory.empty()) {
                load_problems.push_back(std::format("it builds on the skin \"{}\", which isn't installed", current));
                break;
            }

            if (std::find(chain.begin(), chain.end(), directory) != chain.end()) {
                break;
            }

            add(directory);
            SkinManifest own;
            own.inherits = current == DEFAULT_SKIN ? std::string() : std::string(DEFAULT_SKIN);
            own.apply(configs.back());
            current = own.inherits;
        }

        std::filesystem::path fallback = skin_directory(DEFAULT_SKIN);
        bool fallback_listed = std::find(chain.begin(), chain.end(), fallback) != chain.end();

        if (!fallback.empty() && !fallback_listed) {
            chain.push_back(fallback);
        }

        skin_manifest = SkinManifest();
        nlohmann::json defaults = fallback.empty() ? nlohmann::json::object() : read_manifest(fallback);

        if (!fallback_listed) {
            skin_manifest.apply(defaults);
        }

        auto builtin = [&](const std::filesystem::path& directory) {
            std::filesystem::path root = std::filesystem::weakly_canonical(builtin_skins_directory(), ec);
            std::filesystem::path folder = std::filesystem::weakly_canonical(directory, ec);
            auto [end, unused] = std::mismatch(root.begin(), root.end(), folder.begin(), folder.end());
            return end == root.end();
        };

        for (size_t index = configs.size(); index-- > 0;) {
            size_t before = skin_manifest.problems.size();
            skin_manifest.apply(configs[index]);
            std::string label = config_directories[index] == overlay ? std::string("logo/skin.json") : path_utf8(config_directories[index].filename()) + "/skin.json";

            if (!read_errors[index].empty()) {
                load_problems.push_back(std::format("{}: {}", label, read_errors[index]));
            }

            if (!builtin(config_directories[index])) {
                for (size_t problem = before; problem < skin_manifest.problems.size(); problem++) {
                    load_problems.push_back(std::format("{}: {}", label, skin_manifest.problems[problem]));
                }
            }
        }

        SkinManifest known;
        known.apply(defaults);

        for (const auto& [key, value] : skin_manifest.layout) {
            if (!known.layout.contains(key)) {
                load_problems.push_back(std::format("\"layout.{}\" isn't a layout value", key));
            }
        }

        for (const auto& [key_count, overrides] : skin_manifest.keymodes) {
            if (auto layout = overrides.find("layout"); layout != overrides.end() && layout->is_object()) {
                SkinManifest scoped;
                scoped.apply_layout(*layout, "");

                for (const auto& [key, value] : scoped.layout) {
                    if (!known.layout.contains(key)) {
                        load_problems.push_back(std::format("keymodes {}: \"layout.{}\" isn't a layout value", key_count, key));
                    }
                }
            }
        }

        for (size_t index = 0; index < SKIN_COLOR_COUNT; index++) {
            if (!skin_manifest.colors[index]) {
                load_problems.push_back(std::format("there's no color \"{}\", so white is used", SKIN_COLOR_NAMES[index]));
            }
        }

        for (size_t role = 0; role < SKIN_FONT_COUNT; role++) {
            const std::string& file = skin_manifest.fonts[role].file;

            if (!file.empty() && !std::filesystem::exists(find(file), ec)) {
                load_problems.push_back(std::format("the {} font file \"{}\" wasn't found", SKIN_FONT_NAMES[role], file));
                skin_manifest.fonts[role].file.clear();
            }
        }

        SkinManifest own;
        own.apply(configs.empty() ? nlohmann::json::object() : configs.front());
        skin_manifest.name = own.name.empty() ? id : own.name;
        skin_manifest.hidden = own.hidden;
        skin_manifest.inherits = own.inherits;

        for (const std::filesystem::path& directory : chain) {
            if (!builtin(directory)) {
                check_files(directory);
            }
        }

        for (const std::string& problem : load_problems) {
            std::cerr << "Skin \"" << id << "\": " << problem << std::endl;
        }

        title_gradient = std::make_unique<olc::Sprite>(find(GRADIENT_FILE).string());
        layout_values(0);
        load_fonts();
        start_image_loads();
    }

    void Skin::check_files(const std::filesystem::path& directory) {
        std::error_code ec;
        std::string folder = directory == overlay ? std::string(LOGO_FOLDER) : path_utf8(directory.filename());

        for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
            std::string name = path_utf8(entry.path().filename());

            if (entry.is_directory(ec)) {
                if (keymode_folder(name)) {
                    check_files(entry.path());
                }
                else if (name != LOGO_FOLDER || directory == overlay) {
                    load_problems.push_back(std::format("{}: the folder \"{}\" isn't used; key count folders are named like 7k", folder, name));
                }

                continue;
            }

            std::string extension = lowercase(path_utf8(entry.path().extension()));
            std::string stem = path_utf8(entry.path().stem());

            if (extension == ".png") {
                std::string_view base = without_number_suffix(without_number_suffix(stem, '-'), '_');

                if (!is_image_stem(base)) {
                    load_problems.push_back(std::format("{}: \"{}\" isn't an image Nascent uses", folder, name));
                }
            }
            else if (std::find(SOUND_EXTENSIONS.begin(), SOUND_EXTENSIONS.end(), extension) != SOUND_EXTENSIONS.end()) {
                bool known = stem.starts_with(MENU_PLUCK_PREFIX) || std::find(SOUND_STEMS.begin(), SOUND_STEMS.end(), stem) != SOUND_STEMS.end();

                if (!known) {
                    load_problems.push_back(std::format("{}: \"{}\" isn't a sound Nascent uses", folder, name));
                }
            }
        }
    }

    void Skin::load_fonts() {
        std::array<olc::Font*, SKIN_FONT_COUNT> targets = {&display_font_large, &display_font_medium, &display_font_small, &judgement_font, &combo_font};
        std::vector<std::string> fallbacks;

        if (std::filesystem::exists(SYMBOL_FONT_PATH)) {
            fallbacks.emplace_back(SYMBOL_FONT_PATH);
        }

        if (std::filesystem::exists(CJK_FONT_PATH)) {
            fallbacks.emplace_back(CJK_FONT_PATH);
        }

        for (const std::filesystem::path& font : system_fallback_fonts()) {
            fallbacks.push_back(path_utf8(font));
        }

        for (size_t role = 0; role < SKIN_FONT_COUNT; role++) {
            const SkinFontSpec& spec = skin_manifest.fonts[role];
            double reference = spec.size > 0 ? spec.size : DEFAULT_FONT_SIZES[role];
            uint16_t size = (uint16_t)std::max<long>(1, std::lround(reference * font_scale));
            *targets[role] = {find(spec.file.empty() ? std::string(DISPLAY_FONT_FILE) : spec.file).string(), size};

            for (const std::string& fallback : fallbacks) {
                targets[role]->AddFallbackFont(fallback);
            }
        }

        for (HitScore h : ALL_HIT_SCORES) {
            hit_score_decals[(size_t)h] = render_text_decal(judgement_font, hit_score_name(h), olc::WHITE);
        }
    }

    void Skin::unload_fonts() {
        for (const olc::Font* font : {&display_font_large, &display_font_medium, &display_font_small, &judgement_font, &combo_font}) {
            text_cache_forget(*font);
        }
    }

    void Skin::set_font_scale(double scale) {
        if (scale == font_scale) {
            return;
        }

        font_scale = scale;
        unload_fonts();
        load_fonts();
    }

    void Skin::unload() {
        unload_fonts();

        for (std::optional<int32_t>& sound : sounds) {
            if (sound && *sound != -1) {
                game->audio().engine().UnloadSound(*sound);
            }

            sound.reset();
        }

        for (std::optional<std::vector<int32_t>>& pool : voices) {
            if (pool) {
                for (int32_t sound : *pool) {
                    game->audio().engine().UnloadSound(sound);
                }
            }

            pool.reset();
        }

        if (menu_plucks) {
            for (int32_t sound : *menu_plucks) {
                game->audio().engine().UnloadSound(sound);
            }

            menu_plucks.reset();
        }

        clear_lookups();
        image_files.clear();
        decals.clear();
        pending_decals.clear();
        chain.clear();
    }

    void Skin::clear_lookups() {
        manifest_lookup.fill(nullptr);
        layout_lookup.fill(nullptr);

        for (std::optional<LayoutValues>& values : layout_tables) {
            values.reset();
        }

        for (std::vector<int32_t>& lookup : image_lookup) {
            lookup.clear();
        }

        resolved_images.clear();
    }

    const std::string& Skin::get_id() const {
        return id;
    }

    const SkinManifest& Skin::manifest() const {
        return skin_manifest;
    }

    const SkinManifest& Skin::manifest(uint8_t key_count) const {
        if (key_count > MAX_LANES) {
            return build_manifest(key_count);
        }

        const SkinManifest*& cached = manifest_lookup[key_count];

        if (cached == nullptr) {
            cached = &build_manifest(key_count);
        }

        return *cached;
    }

    const SkinManifest& Skin::build_manifest(uint8_t key_count) const {
        auto overrides = skin_manifest.keymodes.find(key_count);

        if (overrides == skin_manifest.keymodes.end()) {
            return skin_manifest;
        }

        auto cached = keymode_manifests.find(key_count);

        if (cached == keymode_manifests.end()) {
            SkinManifest merged = skin_manifest;
            merged.apply_playfield(overrides->second, "");

            if (auto layout = overrides->second.find("layout"); layout != overrides->second.end() && layout->is_object()) {
                merged.apply_layout(*layout, "");
            }

            cached = keymode_manifests.emplace(key_count, std::move(merged)).first;
        }

        return cached->second;
    }

    const std::vector<std::string>& Skin::problems() const {
        return load_problems;
    }

    void Skin::set_layout_key_count(uint8_t key_count) {
        layout_key_count = key_count;
    }

    uint8_t Skin::get_layout_key_count() const {
        return layout_key_count;
    }

    double Skin::layout(std::string_view key) const {
        return layout(key, layout_key_count);
    }

    double Skin::layout(std::string_view key, uint8_t key_count) const {
        const LayoutValues& values = layout_values(key_count);

        if (auto found = values.find(key); found != values.end()) {
            return found->second;
        }

        return missing_layout_value(key);
    }

    const Skin::LayoutValues& Skin::layout_values(uint8_t key_count) const {
        if (key_count <= MAX_LANES && layout_lookup[key_count] != nullptr) {
            return *layout_lookup[key_count];
        }

        uint8_t table = key_count > 0 && key_count <= MAX_LANES && skin_manifest.keymodes.contains(key_count) ? key_count : 0;
        std::optional<LayoutValues>& values = layout_tables[table];

        if (!values) {
            const SkinManifest& source = table == 0 ? skin_manifest : manifest(table);
            values.emplace(source.layout.begin(), source.layout.end());
        }

        if (key_count <= MAX_LANES) {
            layout_lookup[key_count] = &*values;
        }

        return *values;
    }

    double Skin::missing_layout_value(std::string_view key) const {
        if (missing_layout.emplace(key).second) {
            std::cerr << "Skin layout value \"" << key << "\" is missing from the default skin" << std::endl;
        }

        return 0;
    }

    std::filesystem::path Skin::find(const std::string& file) const {
        std::error_code ec;

        for (const std::filesystem::path& directory : chain) {
            if (std::filesystem::exists(directory / utf8_path(file), ec)) {
                return directory / utf8_path(file);
            }
        }

        return chain.empty() ? utf8_path(file) : chain.back() / utf8_path(file);
    }

    std::optional<std::filesystem::path> Skin::find_sound(const std::string& stem) const {
        std::error_code ec;

        for (const std::filesystem::path& directory : chain) {
            for (const char* extension : SOUND_EXTENSIONS) {
                std::filesystem::path path = directory / (stem + extension);

                if (std::filesystem::exists(path, ec)) {
                    return path;
                }
            }
        }

        return std::nullopt;
    }

    Skin::ImageLanes Skin::image_paths(const std::string& stem, uint8_t key_count) const {
        std::error_code ec;

        auto frames_of = [&](const std::filesystem::path& folder, const std::string& name) {
            ImageFrames frames;

            if (std::filesystem::exists(folder / (name + ".png"), ec)) {
                frames.push_back(folder / (name + ".png"));
                return frames;
            }

            for (size_t frame = 0; frame < MAX_ANIMATION_FRAMES; frame++) {
                std::filesystem::path path = folder / std::format("{}-{}.png", name, frame);

                if (!std::filesystem::exists(path, ec)) {
                    break;
                }

                frames.push_back(path);
            }

            return frames;
        };

        for (const std::filesystem::path& directory : chain) {
            std::vector<std::filesystem::path> folders;

            if (key_count > 0) {
                folders.push_back(directory / std::format("{}k", key_count));
            }

            folders.push_back(directory);

            for (const std::filesystem::path& folder : folders) {
                ImageLanes lanes;

                for (uint8_t lane = 1; lane <= MAX_LANES; lane++) {
                    ImageFrames frames = frames_of(folder, std::format("{}_{}", stem, lane));

                    if (frames.empty()) {
                        break;
                    }

                    lanes.push_back(std::move(frames));
                }

                if (lanes.empty()) {
                    ImageFrames frames = frames_of(folder, stem);

                    if (!frames.empty()) {
                        lanes.push_back(std::move(frames));
                    }
                }

                if (!lanes.empty()) {
                    return lanes;
                }
            }
        }

        return {};
    }

    void Skin::start_image_loads() {
        std::set<uint8_t> key_counts = {0};
        std::error_code ec;

        for (const std::filesystem::path& directory : chain) {
            for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
                if (std::optional<uint8_t> keys = keymode_folder(path_utf8(entry.path().filename())); keys && entry.is_directory(ec)) {
                    key_counts.insert(*keys);
                }
            }
        }

        for (uint8_t key_count : key_counts) {
            ImageFiles& files = image_files[key_count];
            SnapFiles& snaps = snap_files[key_count];
            bool tiled = manifest(key_count).hold_style == HoldStyle::TILE;

            for (size_t slot = 0; slot < SKIN_IMAGE_COUNT; slot++) {
                files[slot] = image_paths(IMAGE_SLOTS[slot].stem, key_count);
                queue_decals(files[slot], !tiled_body((SkinImage)slot) || !tiled);
            }

            auto depth = [&](const ImageLanes& lanes) {
                if (lanes.empty() || lanes.front().empty()) {
                    return chain.size();
                }

                std::filesystem::path folder = lanes.front().front().parent_path();

                for (size_t index = 0; index < chain.size(); index++) {
                    if (folder == chain[index] || folder.parent_path() == chain[index]) {
                        return index;
                    }
                }

                return chain.size();
            };

            size_t plain_note = depth(files[(size_t)SkinImage::NOTE]);
            std::array<size_t, SNAP_IMAGES.size()> plain = {plain_note, std::min(plain_note, depth(files[(size_t)SkinImage::HOLD_HEAD]))};

            for (size_t kind = 0; kind < SNAP_IMAGES.size(); kind++) {
                for (size_t snap = 0; snap < NOTE_SNAP_COUNT; snap++) {
                    snaps[kind][snap] = image_paths(snap_stem(SNAP_IMAGES[kind], snap), key_count);

                    if (depth(snaps[kind][snap]) > plain[kind]) {
                        snaps[kind][snap].clear();
                    }

                    queue_decals(snaps[kind][snap], true);
                }
            }
        }
    }

    void Skin::queue_decals(const ImageLanes& lanes, bool clamp) {
        for (const ImageFrames& frames : lanes) {
            for (const std::filesystem::path& path : frames) {
                DecalKey key = {path, clamp};

                if (pending_decals.contains(key) || decals.contains(key)) {
                    continue;
                }

                auto build = [path] {
                    return MipDecal::build_levels(std::make_unique<olc::Sprite>(path.string()));
                };
                pending_decals.emplace(key, run_detached(build));
            }
        }
    }

    const MipDecal& Skin::decal(const std::filesystem::path& path, bool clamp) {
        DecalKey key = {path, clamp};

        if (auto found = decals.find(key); found != decals.end()) {
            return found->second;
        }

        std::vector<std::unique_ptr<olc::Sprite>> levels;

        if (auto pending = pending_decals.find(key); pending != pending_decals.end()) {
            levels = pending->second.get();
            pending_decals.erase(pending);
        }
        else {
            levels = MipDecal::build_levels(std::make_unique<olc::Sprite>(path.string()));
        }

        return decals.emplace(key, MipDecal(std::move(levels), clamp)).first->second;
    }

    Skin::ResolvedImage* Skin::find_image(SkinImage image, uint8_t lane, uint8_t key_count, std::optional<uint8_t> shown_snap, std::optional<ResolvedImage>& uncached) {
        ResolvedImage* resolved = nullptr;

        if (key_count <= MAX_LANES && lane < MAX_LANES) {
            std::vector<int32_t>& lookup = image_lookup[key_count];

            if (lookup.empty()) {
                lookup.assign(IMAGE_LOOKUP_SIZE, UNRESOLVED_IMAGE);
            }

            int32_t& slot = lookup[((size_t)image * (NOTE_SNAP_COUNT + 1) + (shown_snap ? *shown_snap + 1 : 0)) * MAX_LANES + lane];

            if (slot == UNRESOLVED_IMAGE) {
                std::optional<ResolvedImage> found = resolve_image(image, lane, key_count, shown_snap);
                slot = found ? (int32_t)resolved_images.size() : MISSING_IMAGE;

                if (found) {
                    resolved_images.push_back(std::move(*found));
                }
            }

            if (slot == MISSING_IMAGE) {
                return nullptr;
            }

            resolved = &resolved_images[(size_t)slot];
        }
        else {
            uncached = resolve_image(image, lane, key_count, shown_snap);

            if (!uncached) {
                return nullptr;
            }

            resolved = &*uncached;
        }

        return resolved;
    }

    const MipDecal* Skin::frame_decal(ResolvedImage& resolved, size_t frame) {
        const MipDecal*& chosen = resolved.decals[frame];

        if (chosen == nullptr) {
            chosen = &decal((*resolved.frames)[frame], resolved.clamp);
        }

        return *chosen ? chosen : nullptr;
    }

    const MipDecal* Skin::image_frame(SkinImage image, uint8_t key_count, size_t frame) {
        std::optional<ResolvedImage> uncached;
        ResolvedImage* resolved = find_image(image, 0, key_count, std::nullopt, uncached);

        if (resolved == nullptr || resolved->frames->empty()) {
            return nullptr;
        }

        return frame_decal(*resolved, frame % resolved->frames->size());
    }

    size_t Skin::frame_count(SkinImage image, uint8_t key_count) {
        std::optional<ResolvedImage> uncached;
        ResolvedImage* resolved = find_image(image, 0, key_count, std::nullopt, uncached);
        return resolved != nullptr ? resolved->frames->size() : 0;
    }

    const MipDecal* Skin::image(SkinImage image, uint8_t lane, uint8_t key_count, std::optional<uint8_t> snap, std::optional<double> since) {
        std::optional<uint8_t> shown_snap = snap && *snap < NOTE_SNAP_COUNT ? snap : std::nullopt;
        std::optional<ResolvedImage> uncached;
        ResolvedImage* resolved = find_image(image, lane, key_count, shown_snap, uncached);

        if (resolved == nullptr) {
            return nullptr;
        }

        const ImageFrames& frames = *resolved->frames;
        size_t frame = 0;

        if (frames.size() > 1) {
            double fps = resolved->fps;

            if (since) {
                frame = fps > 0 ? std::min(frames.size() - 1, (size_t)(std::max(0.0, *since) * fps)) : 0;
            }
            else {
                frame = fps > 0 ? (size_t)(seconds_since(loaded_at) * fps) % frames.size() : 0;
            }
        }

        return frame_decal(*resolved, frame);
    }

    std::optional<Skin::ResolvedImage> Skin::resolve_image(SkinImage image, uint8_t lane, uint8_t key_count, std::optional<uint8_t> snap) const {
        auto files = image_files.find(key_count);

        if (files == image_files.end()) {
            files = image_files.find(0);
        }

        if (files == image_files.end()) {
            return std::nullopt;
        }

        auto snaps = snap_files.find(files->first);
        size_t slot = (size_t)image;
        const ImageLanes* found = nullptr;
        std::string stem;

        while (found == nullptr) {
            auto kind = std::find(SNAP_IMAGES.begin(), SNAP_IMAGES.end(), (SkinImage)slot);

            if (snap && *snap < NOTE_SNAP_COUNT && kind != SNAP_IMAGES.end() && snaps != snap_files.end()) {
                const ImageLanes& snapped = snaps->second[(size_t)(kind - SNAP_IMAGES.begin())][*snap];

                if (!snapped.empty()) {
                    found = &snapped;
                    stem = snap_stem((SkinImage)slot, *snap);
                    break;
                }
            }

            if (!files->second[slot].empty()) {
                found = &files->second[slot];
                stem = IMAGE_SLOTS[slot].stem;
                break;
            }

            if (!IMAGE_SLOTS[slot].fallback) {
                return std::nullopt;
            }

            slot = (size_t)*IMAGE_SLOTS[slot].fallback;
        }

        const ImageLanes& lanes = *found;

        const SkinManifest& settings = manifest(key_count);
        size_t index = lane;
        auto mapping = settings.lane_images.find(key_count);

        if (mapping != settings.lane_images.end() && lane < mapping->second.size()) {
            index = (size_t)mapping->second[lane] - 1;
        }

        const ImageFrames& frames = lanes[index % lanes.size()];

        if (frames.empty()) {
            return std::nullopt;
        }

        ResolvedImage resolved;
        resolved.frames = &frames;
        resolved.decals.assign(frames.size(), nullptr);
        resolved.fps = settings.frame_rate;
        resolved.clamp = !tiled_body((SkinImage)slot) || settings.hold_style != HoldStyle::TILE;

        if (frames.size() > 1) {
            auto rate = settings.frame_rates.find(stem);

            if (rate == settings.frame_rates.end()) {
                rate = settings.frame_rates.find(IMAGE_SLOTS[slot].stem);
            }

            if (rate != settings.frame_rates.end()) {
                resolved.fps = rate->second;
            }
        }

        return resolved;
    }

    const MipDecal* Skin::judgement_image(HitScore score, uint8_t key_count, std::optional<double> since) {
        std::optional<SkinImage> slot = judgement_slot(score);
        return slot ? image(*slot, 0, key_count, std::nullopt, since) : nullptr;
    }

    const SkinMascot& Skin::mascot() const {
        return skin_manifest.mascot;
    }

    olc::Decal* Skin::hit_score_decal(HitScore score) const {
        return hit_score_decals[(size_t)score].get();
    }

    olc::Pixel Skin::title_gradient_at(double x, double y) const {
        if (title_gradient == nullptr || title_gradient->width == 0 || title_gradient->height == 0) {
            return olc::WHITE;
        }

        x = std::clamp(x, 0.0, 1.0);
        y = std::clamp(y, 0.0, 1.0);
        return title_gradient->GetPixel({(int32_t)(x * (title_gradient->width - 1)), (int32_t)(y * (title_gradient->height - 1))});
    }

    olc::Pixel Skin::color(SkinColor color) const {
        return skin_manifest.colors[(size_t)color].value_or(olc::WHITE);
    }

    olc::Font& Skin::font(SkinFont font) {
        switch (font) {
            case SkinFont::LARGE:
                return display_font_large;
            case SkinFont::MEDIUM:
                return display_font_medium;
            case SkinFont::JUDGEMENT:
                return judgement_font;
            case SkinFont::COMBO:
                return combo_font;
            default:
                return display_font_small;
        }
    }

    olc::Font& Skin::font(UiFont font) {
        switch (font) {
            case UiFont::LARGE:
                return display_font_large;
            case UiFont::MEDIUM:
                return display_font_medium;
            default:
                return display_font_small;
        }
    }

    olc::Pixel Skin::color(UiColor color) const {
        switch (color) {
            case UiColor::TEXT_MUTED:
                return this->color(SkinColor::TEXT_MUTED);
            case UiColor::TEXT_FAINT:
                return this->color(SkinColor::TEXT_FAINT);
            case UiColor::INPUT_BACKGROUND:
                return this->color(SkinColor::INPUT_BACKGROUND);
            case UiColor::INPUT_BACKGROUND_FOCUSED:
                return this->color(SkinColor::INPUT_BACKGROUND_FOCUSED);
            case UiColor::PROGRESS_TRACK:
                return this->color(SkinColor::PROGRESS_TRACK);
            case UiColor::SEPARATOR:
                return this->color(SkinColor::SEPARATOR);
            default:
                return this->color(SkinColor::TEXT);
        }
    }

    olc::Pixel Skin::accent(double position) const {
        return title_gradient_at(position, 0.5);
    }

    olc::Pixel Skin::judgement_color(HitScore score) const {
        switch (score) {
            case HitScore::PERFECT:
                return color(SkinColor::JUDGEMENT_PERFECT);
            case HitScore::AMAZING:
                return color(SkinColor::JUDGEMENT_AMAZING);
            case HitScore::GREAT:
                return color(SkinColor::JUDGEMENT_GREAT);
            case HitScore::GOOD:
                return color(SkinColor::JUDGEMENT_GOOD);
            case HitScore::BAD:
                return color(SkinColor::JUDGEMENT_BAD);
            case HitScore::MISS:
                return color(SkinColor::JUDGEMENT_MISS);
            default:
                return olc::BLANK;
        }
    }

    olc::Pixel Skin::grade_color(Grade grade) const {
        switch (grade) {
            case Grade::X:
                return color(SkinColor::GRADE_X);
            case Grade::S:
                return color(SkinColor::GRADE_S);
            case Grade::A:
                return color(SkinColor::GRADE_A);
            case Grade::B:
                return color(SkinColor::GRADE_B);
            case Grade::C:
                return color(SkinColor::GRADE_C);
            case Grade::D:
                return color(SkinColor::GRADE_D);
            case Grade::E:
                return color(SkinColor::GRADE_E);
            default:
                return color(SkinColor::GRADE_F);
        }
    }

    olc::Pixel Skin::difficulty_color(double difficulty) const {
        const auto& stops = skin_manifest.difficulty_scale;

        if (stops.empty()) {
            return title_gradient_at(difficulty / DIFFICULTY_GRADIENT_MAX, 0.5);
        }

        if (difficulty <= stops.front().first) {
            return stops.front().second;
        }

        for (size_t index = 1; index < stops.size(); index++) {
            if (difficulty <= stops[index].first) {
                const auto& [low_value, low] = stops[index - 1];
                const auto& [high_value, high] = stops[index];
                double t = (difficulty - low_value) / std::max(1e-9, high_value - low_value);
                auto mix = [t](uint8_t a, uint8_t b) {
                    return (uint8_t)std::lround(a + (b - a) * t);
                };
                return olc::Pixel(mix(low.r, high.r), mix(low.g, high.g), mix(low.b, high.b), mix(low.a, high.a));
            }
        }

        return stops.back().second;
    }

    olc::Pixel Skin::rank_color(int64_t rank) const {
        switch (rank) {
            case 1:
                return color(SkinColor::RANK_FIRST);
            case 2:
                return color(SkinColor::RANK_SECOND);
            case 3:
                return color(SkinColor::RANK_THIRD);
            default:
                return color(SkinColor::TEXT_MUTED);
        }
    }

    std::array<olc::vf2d, Skin::GRADE_GLOW_STEPS> Skin::glow_offsets(float radius) {
        static const std::array<olc::vf2d, GRADE_GLOW_STEPS> directions = [] {
            std::array<olc::vf2d, GRADE_GLOW_STEPS> unit;

            for (int step = 0; step < GRADE_GLOW_STEPS; step++) {
                double angle = step * 2.0 * std::numbers::pi / GRADE_GLOW_STEPS;
                unit[step] = {(float)std::cos(angle), (float)std::sin(angle)};
            }

            return unit;
        }();
        std::array<olc::vf2d, GRADE_GLOW_STEPS> offsets;

        for (int step = 0; step < GRADE_GLOW_STEPS; step++) {
            offsets[step] = {directions[step].x * radius, directions[step].y * radius};
        }

        return offsets;
    }

    olc::Pixel Skin::glow_color(olc::Pixel color, float opacity) {
        return fade(color, GRADE_GLOW_ALPHA * opacity);
    }

    void Skin::draw_judgement(HitScore score, olc::Font& font, olc::vf2d baseline, float opacity) {
        if (const MipDecal* picture = judgement_image(score); picture != nullptr && picture->size().y > 0) {
            olc::FontRect metrics = text_bounds(font, "Hgjy");
            float scale = (float)metrics.size.y / picture->size().y;
            MipDecal::Pick level = picture->pick(scale);
            game->window->DrawDecal({baseline.x, baseline.y + metrics.offset.y}, level.decal, {scale / level.factor, scale / level.factor}, fade(olc::WHITE, opacity));
            return;
        }

        std::string_view text = hit_score_name(score);
        olc::Pixel base = judgement_color(score);

        if (score == HitScore::PERFECT) {
            for (olc::vf2d offset : glow_offsets(std::max(1.0f, Ui::pxf(GRADE_GLOW_RADIUS)))) {
                draw_text(font, text, baseline + offset, glow_color(base, opacity), 1.0f);
            }
        }

        draw_text(font, text, baseline, fade(base, opacity), 1.0f);
    }

    void Skin::draw_grade(Grade grade, olc::Font& font, olc::vf2d baseline, float scale, float opacity) {
        std::string_view text = grade_name(grade);

        if (const MipDecal* picture = image((SkinImage)((size_t)SkinImage::GRADE_X + (size_t)grade), 0, 0); picture != nullptr && picture->size().y > 0) {
            olc::FontRect bounds = text_bounds(font, text);
            float height = (float)bounds.size.y * scale;
            float drawn = height / picture->size().y;
            float width = picture->size().x * drawn;
            olc::vf2d corner = {baseline.x + ((float)bounds.offset.x + (float)bounds.size.x / 2.0f) * scale - width / 2.0f, baseline.y + (float)bounds.offset.y * scale};
            MipDecal::Pick level = picture->pick(drawn);
            game->window->DrawDecal(corner, level.decal, {drawn / level.factor, drawn / level.factor}, fade(olc::WHITE, opacity));
            return;
        }
        olc::Pixel base = grade_color(grade);
        double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();

        if (grade == Grade::X || grade == Grade::S) {
            olc::Pixel glow = grade == Grade::X ? title_gradient_at(std::fmod(seconds * GRADE_GLOW_CYCLE + baseline.x * 0.001, 1.0), 0.5) : base;
            glow = glow_color(glow, opacity);

            for (olc::vf2d offset : glow_offsets(std::max(1.0f, Ui::pxf(GRADE_GLOW_RADIUS) * scale))) {
                draw_text(font, text, baseline + offset, glow, scale);
            }
        }

        if (grade == Grade::X) {
            double shine = 0.5 + 0.5 * std::sin(seconds * GRADE_SHIMMER_SPEED + baseline.x * 0.01);
            auto mix = [shine](uint8_t channel) {
                return (uint8_t)std::lround(channel + (255 - channel) * shine * 0.7);
            };
            base = olc::Pixel(mix(base.r), mix(base.g), mix(base.b), base.a);
        }

        draw_text(font, text, baseline, fade(base, opacity), scale);
    }

    int32_t Skin::sound(SkinSound sound) {
        std::optional<int32_t>& slot = sounds[(size_t)sound];

        if (!slot) {
            std::optional<std::filesystem::path> path = find_sound(SOUND_STEMS[(size_t)sound]);
            slot = path ? game->audio().engine().LoadSound(path->string()) : -1;
        }

        return *slot;
    }

    int32_t Skin::voice(SkinSound sound) {
        std::optional<std::vector<int32_t>>& pool = voices[(size_t)sound];

        if (!pool) {
            pool.emplace();

            if (std::optional<std::filesystem::path> path = find_sound(SOUND_STEMS[(size_t)sound])) {
                for (size_t index = 0; index < HITSOUND_VOICES; index++) {
                    int32_t id = game->audio().engine().LoadSound(path->string());

                    if (id != -1) {
                        pool->push_back(id);
                    }
                }
            }
        }

        if (pool->empty()) {
            return -1;
        }

        return (*pool)[next_voice[(size_t)sound]++ % pool->size()];
    }

    int32_t Skin::hitsound_voice() {
        return voice(SkinSound::HITSOUND);
    }

    int32_t Skin::combo_break_voice() {
        return voice(SkinSound::COMBO_BREAK);
    }

    int32_t Skin::judgement_voice(HitScore score) {
        std::optional<SkinImage> slot = judgement_slot(score);

        if (!slot) {
            return -1;
        }

        return voice((SkinSound)((size_t)SkinSound::JUDGEMENT_PERFECT + ((size_t)*slot - (size_t)SkinImage::JUDGEMENT_PERFECT)));
    }

    int32_t Skin::get_random_menu_pluck() {
        if (!menu_plucks) {
            menu_plucks.emplace();
            std::error_code ec;

            for (const std::filesystem::path& directory : chain) {
                for (size_t index = 0; index < MAX_MENU_PLUCKS; index++) {
                    for (const char* extension : SOUND_EXTENSIONS) {
                        std::filesystem::path path = directory / std::format("menu_pluck_{}{}", index, extension);

                        if (std::filesystem::exists(path, ec)) {
                            int32_t sound = game->audio().engine().LoadSound(path.string());

                            if (sound != -1) {
                                menu_plucks->push_back(sound);
                            }

                            break;
                        }
                    }
                }

                if (!menu_plucks->empty()) {
                    break;
                }
            }
        }

        if (menu_plucks->empty()) {
            return -1;
        }

        return menu_plucks->at((size_t)std::rand() % menu_plucks->size());
    }
}
