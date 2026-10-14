#pragma once

#include "json.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nascent {
    struct ReleaseVersion {
        uint32_t major = 0;
        uint32_t minor = 0;
        uint32_t patch = 0;
        std::string label;
        uint64_t label_number = 0;
        bool prerelease = false;

        std::strong_ordering operator<=>(const ReleaseVersion& other) const;
        bool operator==(const ReleaseVersion& other) const;
    };

    enum class UpdatePackage {
        ZIP,
        APPIMAGE,
    };

    struct ReleaseAsset {
        std::string name;
        std::string url;
        uint64_t size = 0;
    };

    struct UpdateRelease {
        std::string tag;
        std::string page_url;
        ReleaseAsset package;
        ReleaseAsset checksums;
    };

    std::optional<ReleaseVersion> parse_release_version(std::string_view text);
    std::string update_package_name(std::string_view tag, std::string_view os, std::string_view arch, UpdatePackage package);
    std::optional<UpdateRelease> newest_update(const nlohmann::json& releases, const ReleaseVersion& current, std::string_view os, std::string_view arch, UpdatePackage package);
    std::optional<std::string> checksum_for(std::string_view checksums, std::string_view file_name);

    inline constexpr const char* CHECKSUMS_FILE_NAME = "SHA256SUMS";
}
