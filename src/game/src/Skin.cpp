#include "Skin.h"
#include "Game.h"
#include "Const.h"
#include "Platform.h"
#include "SettingsManager.h"
#include "TextCache.h"
#include "Utf8Path.h"

#include <algorithm>
#include <charconv>
#include <cmath>
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
        }};

        constexpr std::array<const char*, SKIN_SOUND_COUNT> SOUND_STEMS = {"hitsound", "idle_loop", "account_loop"};
        constexpr std::array<const char*, 4> SOUND_EXTENSIONS = {".mp3", ".wav", ".ogg", ".flac"};

        nlohmann::json read_manifest(const std::filesystem::path& directory) {
            std::ifstream file(directory / Skin::MANIFEST_FILE);

            if (!file) {
                return nlohmann::json::object();
            }

            nlohmann::json config = nlohmann::json::parse(file, nullptr, false);
            return config.is_object() ? config : nlohmann::json::object();
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
    }

    void SkinManifest::apply_colors(const nlohmann::json& config) {
        auto entries = config.is_object() ? config.find("colors") : config.end();

        if (!config.is_object() || entries == config.end() || !entries->is_object()) {
            return;
        }

        for (const auto& [key, value] : entries->items()) {
            auto slot = std::find_if(SKIN_COLOR_NAMES.begin(), SKIN_COLOR_NAMES.end(), [&](const char* name) {
                return key == name;
            });

            if (slot == SKIN_COLOR_NAMES.end()) {
                continue;
            }

            std::optional<olc::Pixel> color = value.is_string() ? parse_color(value.get<std::string>()) : std::nullopt;

            if (!color) {
                std::cerr << "Skin color \"" << key << "\" must look like #rrggbb or #rrggbbaa" << std::endl;
                continue;
            }

            colors[(size_t)(slot - SKIN_COLOR_NAMES.begin())] = color;
        }
    }

    void SkinManifest::apply(const nlohmann::json& config) {
        if (!config.is_object()) {
            return;
        }

        auto has = [&](const char* key, nlohmann::json::value_t type) {
            auto it = config.find(key);
            return it != config.end() && (it->type() == type || (type == nlohmann::json::value_t::number_float && it->is_number()));
        };

        if (has("name", nlohmann::json::value_t::string)) {
            name = config["name"].get<std::string>();
        }

        if (config.contains("inherits")) {
            inherits = config["inherits"].is_string() ? config["inherits"].get<std::string>() : std::string();
        }

        if (has("hidden", nlohmann::json::value_t::boolean)) {
            hidden = config["hidden"].get<bool>();
        }

        if (has("tint", nlohmann::json::value_t::string)) {
            tint = config["tint"].get<std::string>() == "none" ? SkinTint::NONE : SkinTint::GRADIENT;
        }
        else if (has("tint_notes", nlohmann::json::value_t::boolean)) {
            tint = config["tint_notes"].get<bool>() ? SkinTint::GRADIENT : SkinTint::NONE;
        }

        if (has("hold_style", nlohmann::json::value_t::string)) {
            std::string style = config["hold_style"].get<std::string>();
            hold_style = style == "stretch" ? HoldStyle::STRETCH : style == "tile" ? HoldStyle::TILE
                                                                                   : HoldStyle::REPEAT;
        }

        if (has("hold_body_aspect", nlohmann::json::value_t::number_float)) {
            hold_body_aspect = std::clamp(config["hold_body_aspect"].get<double>(), 0.001, 1000.0);
        }

        if (has("hold_body_head_offset", nlohmann::json::value_t::number_float)) {
            hold_body_head_offset = std::clamp(config["hold_body_head_offset"].get<double>(), -4.0, 4.0);
        }

        if (has("hold_body_tail_offset", nlohmann::json::value_t::number_float)) {
            hold_body_tail_offset = std::clamp(config["hold_body_tail_offset"].get<double>(), -4.0, 4.0);
        }

        if (has("key_spacing", nlohmann::json::value_t::number_float)) {
            key_spacing = std::clamp(config["key_spacing"].get<double>(), 0.0, 4.0);
        }

        if (has("hold_alpha", nlohmann::json::value_t::number_float)) {
            hold_alpha = clamp_alpha(config["hold_alpha"]);
        }

        if (has("receptor_alpha", nlohmann::json::value_t::number_float)) {
            receptor_alpha = clamp_alpha(config["receptor_alpha"]);
        }

        apply_colors(config);

        if (has("lane_images", nlohmann::json::value_t::object)) {
            for (const auto& [keys, lanes] : config["lane_images"].items()) {
                int key_count = std::atoi(keys.c_str());

                if (key_count < 1 || key_count > MAX_LANES || !lanes.is_array()) {
                    continue;
                }

                std::vector<uint8_t> mapping;

                for (const nlohmann::json& lane : lanes) {
                    mapping.push_back(lane.is_number() ? (uint8_t)std::clamp(lane.get<int>(), 1, (int)MAX_LANES) : (uint8_t)1);
                }

                lane_images[(uint8_t)key_count] = std::move(mapping);
            }
        }
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

                if (!entry.is_directory(ec) || !is_valid_id(id) || !seen.insert(id).second) {
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

    void Skin::reload(const std::string& id) {
        unload();
        this->id = id;
        load();
    }

    void Skin::load() {
        if (skin_directory(id).empty()) {
            std::cerr << "Skin \"" << id << "\" was not found, using the default skin" << std::endl;
            id = DEFAULT_SKIN;
        }

        std::vector<nlohmann::json> configs;
        std::string current = id;

        while (!current.empty() && chain.size() < MAX_INHERITANCE_DEPTH) {
            std::filesystem::path directory = skin_directory(current);

            if (directory.empty() || std::find(chain.begin(), chain.end(), directory) != chain.end()) {
                break;
            }

            chain.push_back(directory);
            configs.push_back(read_manifest(directory));

            SkinManifest own;
            own.inherits = current == DEFAULT_SKIN ? std::string() : std::string(DEFAULT_SKIN);
            own.apply(configs.back());
            current = own.inherits;
        }

        std::filesystem::path fallback = skin_directory(DEFAULT_SKIN);

        if (!fallback.empty() && std::find(chain.begin(), chain.end(), fallback) == chain.end()) {
            chain.push_back(fallback);
        }

        skin_manifest = SkinManifest();

        if (!fallback.empty()) {
            skin_manifest.apply_colors(read_manifest(fallback));
        }

        for (auto config = configs.rbegin(); config != configs.rend(); ++config) {
            skin_manifest.apply(*config);
        }

        for (size_t index = 0; index < SKIN_COLOR_COUNT; index++) {
            if (!skin_manifest.colors[index]) {
                std::cerr << "Skin \"" << id << "\" has no color \"" << SKIN_COLOR_NAMES[index] << "\", using white" << std::endl;
            }
        }

        SkinManifest own;
        own.apply(configs.empty() ? nlohmann::json::object() : configs.front());
        skin_manifest.name = own.name.empty() ? id : own.name;
        skin_manifest.hidden = own.hidden;
        skin_manifest.inherits = own.inherits;

        title_gradient = std::make_unique<olc::Sprite>(find(GRADIENT_FILE).string());
        load_fonts();
    }

    void Skin::load_fonts() {
        auto size = [&](uint16_t reference) {
            return (uint16_t)std::max<long>(1, std::lround(reference * font_scale));
        };

        std::string font_path = find(DISPLAY_FONT_FILE).string();
        display_font_small = {font_path, size(DISPLAY_FONT_SMALL_SIZE)};
        display_font_medium = {font_path, size(DISPLAY_FONT_MEDIUM_SIZE)};
        display_font_large = {font_path, size(DISPLAY_FONT_LARGE_SIZE)};

        if (std::filesystem::exists(SYMBOL_FONT_PATH)) {
            display_font_small.AddFallbackFont(SYMBOL_FONT_PATH);
            display_font_medium.AddFallbackFont(SYMBOL_FONT_PATH);
            display_font_large.AddFallbackFont(SYMBOL_FONT_PATH);
        }

        for (HitScore h : ALL_HIT_SCORES) {
            hit_score_decals[(size_t)h] = SpriteDecal::adopt(display_font_large.RenderStringToDecal(std::string(hit_score_name(h)), judgement_color(h)));
        }

        game_paused_decal = SpriteDecal::adopt(display_font_large.RenderStringToDecal("Game Paused", color(SkinColor::TEXT)));
    }

    void Skin::unload_fonts() {
        for (const olc::Font* font : {&display_font_large, &display_font_medium, &display_font_small}) {
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
                game->get_audio().UnloadSound(*sound);
            }

            sound.reset();
        }

        if (menu_plucks) {
            for (int32_t sound : *menu_plucks) {
                game->get_audio().UnloadSound(sound);
            }

            menu_plucks.reset();
        }

        for (std::optional<std::vector<SpriteDecal>>& slot : images) {
            slot.reset();
        }

        chain.clear();
    }

    const std::string& Skin::get_id() const {
        return id;
    }

    const SkinManifest& Skin::manifest() const {
        return skin_manifest;
    }

    std::filesystem::path Skin::find(const std::string& file) const {
        std::error_code ec;

        for (const std::filesystem::path& directory : chain) {
            if (std::filesystem::exists(directory / file, ec)) {
                return directory / file;
            }
        }

        return chain.empty() ? std::filesystem::path(file) : chain.back() / file;
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

    const std::vector<SpriteDecal>& Skin::lane_images(SkinImage image) {
        std::optional<std::vector<SpriteDecal>>& slot = images[(size_t)image];

        if (!slot) {
            slot.emplace();
            std::error_code ec;
            std::string stem = IMAGE_SLOTS[(size_t)image].stem;
            bool clamp = image != SkinImage::HOLD_BODY || skin_manifest.hold_style != HoldStyle::TILE;

            for (const std::filesystem::path& directory : chain) {
                for (uint8_t lane = 1; lane <= MAX_LANES; lane++) {
                    std::filesystem::path path = directory / std::format("{}_{}.png", stem, lane);

                    if (!std::filesystem::exists(path, ec)) {
                        break;
                    }

                    slot->emplace_back(std::make_unique<olc::Sprite>(path.string()), true, clamp);
                }

                if (slot->empty() && std::filesystem::exists(directory / (stem + ".png"), ec)) {
                    slot->emplace_back(std::make_unique<olc::Sprite>((directory / (stem + ".png")).string()), true, clamp);
                }

                if (!slot->empty()) {
                    break;
                }
            }
        }

        std::optional<SkinImage> fallback = IMAGE_SLOTS[(size_t)image].fallback;
        return slot->empty() && fallback ? lane_images(*fallback) : *slot;
    }

    olc::Decal* Skin::image(SkinImage image, uint8_t lane, uint8_t key_count) {
        const std::vector<SpriteDecal>& decals = lane_images(image);

        if (decals.empty()) {
            return nullptr;
        }

        size_t index = lane;
        auto mapping = skin_manifest.lane_images.find(key_count);

        if (mapping != skin_manifest.lane_images.end() && lane < mapping->second.size()) {
            index = (size_t)mapping->second[lane] - 1;
        }

        return decals[index % decals.size()].get();
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

    int32_t Skin::sound(SkinSound sound) {
        std::optional<int32_t>& slot = sounds[(size_t)sound];

        if (!slot) {
            std::optional<std::filesystem::path> path = find_sound(SOUND_STEMS[(size_t)sound]);
            slot = path ? game->get_audio().LoadSound(path->string()) : -1;
        }

        return *slot;
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
                            int32_t sound = game->get_audio().LoadSound(path.string());

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
