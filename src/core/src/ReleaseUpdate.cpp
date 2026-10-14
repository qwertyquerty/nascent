#include "ReleaseUpdate.h"

#include <cctype>
#include <charconv>
#include <format>
#include <limits>
#include <tuple>

namespace nascent {
    static std::optional<uint32_t> parse_number(std::string_view text) {
        uint32_t value = 0;

        if (text.empty() || (text.size() > 1 && text[0] == '0')) {
            return std::nullopt;
        }

        auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);

        if (error != std::errc() || end != text.data() + text.size()) {
            return std::nullopt;
        }

        return value;
    }

    std::strong_ordering ReleaseVersion::operator<=>(const ReleaseVersion& other) const {
        if (auto order = std::tie(major, minor, patch) <=> std::tie(other.major, other.minor, other.patch); order != 0) {
            return order;
        }

        if (prerelease != other.prerelease) {
            return prerelease ? std::strong_ordering::less : std::strong_ordering::greater;
        }

        if (auto order = label <=> other.label; order != 0) {
            return order;
        }

        return label_number <=> other.label_number;
    }

    bool ReleaseVersion::operator==(const ReleaseVersion& other) const {
        return (*this <=> other) == 0;
    }

    std::optional<ReleaseVersion> parse_release_version(std::string_view text) {
        ReleaseVersion version;
        uint32_t* numbers[] = {&version.major, &version.minor, &version.patch};

        for (size_t index = 0; index < 3; index++) {
            size_t dot = text.find('.');
            std::string_view part = text.substr(0, dot);
            std::optional<uint32_t> number = parse_number(part);

            if (!number || (index < 2 && dot == std::string_view::npos)) {
                return std::nullopt;
            }

            *numbers[index] = *number;
            text = dot == std::string_view::npos ? std::string_view() : text.substr(dot + 1);

            if (index == 2 && dot != std::string_view::npos && text.empty()) {
                return std::nullopt;
            }
        }

        if (text.empty()) {
            return version;
        }

        for (char c : text) {
            if (!std::isalnum((unsigned char)c)) {
                return std::nullopt;
            }
        }

        size_t digits = text.size();

        while (digits > 0 && std::isdigit((unsigned char)text[digits - 1])) {
            digits--;
        }

        std::string_view number = text.substr(digits);

        if (number.size() > std::numeric_limits<uint64_t>::digits10) {
            return std::nullopt;
        }

        version.prerelease = true;
        version.label.reserve(digits);

        for (char c : text.substr(0, digits)) {
            version.label.push_back((char)std::tolower((unsigned char)c));
        }

        if (!number.empty()) {
            std::from_chars(number.data(), number.data() + number.size(), version.label_number);
        }

        return version;
    }

    std::string update_package_name(std::string_view tag, std::string_view os, std::string_view arch, UpdatePackage package) {
        return std::format("nascent-{}-{}-{}.{}", tag, os, arch, package == UpdatePackage::APPIMAGE ? "AppImage" : "zip");
    }

    static std::string string_field(const nlohmann::json& object, const char* name) {
        auto field = object.find(name);
        return field != object.end() && field->is_string() ? field->get<std::string>() : std::string();
    }

    static std::optional<ReleaseAsset> find_asset(const nlohmann::json& release, std::string_view name) {
        auto assets = release.find("assets");

        if (assets == release.end() || !assets->is_array()) {
            return std::nullopt;
        }

        for (const nlohmann::json& asset : *assets) {
            if (!asset.is_object() || string_field(asset, "name") != name) {
                continue;
            }

            ReleaseAsset found{std::string(name), string_field(asset, "browser_download_url"), 0};
            auto size = asset.find("size");

            if (size != asset.end() && size->is_number_unsigned()) {
                found.size = size->get<uint64_t>();
            }

            if (!found.url.starts_with("https://")) {
                return std::nullopt;
            }

            return found;
        }

        return std::nullopt;
    }

    std::optional<UpdateRelease> newest_update(const nlohmann::json& releases, const ReleaseVersion& current, std::string_view os, std::string_view arch, UpdatePackage package) {
        std::optional<UpdateRelease> newest;
        std::optional<ReleaseVersion> newest_version;

        if (!releases.is_array()) {
            return std::nullopt;
        }

        for (const nlohmann::json& release : releases) {
            if (!release.is_object() || release.value("draft", false)) {
                continue;
            }

            std::string tag = string_field(release, "tag_name");
            std::optional<ReleaseVersion> version = parse_release_version(tag);

            if (!version || *version <= current || (version->prerelease && !current.prerelease) || (newest_version && *version <= *newest_version)) {
                continue;
            }

            std::optional<ReleaseAsset> asset = find_asset(release, update_package_name(tag, os, arch, package));
            std::optional<ReleaseAsset> checksums = find_asset(release, CHECKSUMS_FILE_NAME);

            if (!asset || !checksums) {
                continue;
            }

            newest = UpdateRelease{tag, string_field(release, "html_url"), *asset, *checksums};
            newest_version = version;
        }

        return newest;
    }

    std::optional<std::string> checksum_for(std::string_view checksums, std::string_view file_name) {
        while (!checksums.empty()) {
            size_t end = checksums.find('\n');
            std::string_view line = checksums.substr(0, end);
            checksums = end == std::string_view::npos ? std::string_view() : checksums.substr(end + 1);

            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.remove_suffix(1);
            }

            if (line.size() < 66 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*') || line.substr(66) != file_name) {
                continue;
            }

            std::string hash;

            for (char c : line.substr(0, 64)) {
                if (!std::isxdigit((unsigned char)c)) {
                    return std::nullopt;
                }

                hash.push_back((char)std::tolower((unsigned char)c));
            }

            return hash;
        }

        return std::nullopt;
    }
}
