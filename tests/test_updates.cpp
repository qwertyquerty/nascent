#include "ReleaseUpdate.h"
#include "TestSupport.h"

#include <optional>
#include <string>

using namespace nascent;
using namespace nascent::test;

static ReleaseVersion version(const char* text) {
    std::optional<ReleaseVersion> parsed = parse_release_version(text);

    if (!parsed) {
        std::cerr << "could not parse " << text << std::endl;
        std::exit(1);
    }

    return *parsed;
}

static nlohmann::json release(const std::string& tag, const std::vector<std::string>& assets, bool draft = false) {
    nlohmann::json files = nlohmann::json::array();

    for (const std::string& name : assets) {
        files.push_back({{"name", name}, {"browser_download_url", "https://github.com/qwertyquerty/nascent/releases/download/" + tag + "/" + name}, {"size", 1234}});
    }

    return {{"tag_name", tag}, {"draft", draft}, {"html_url", "https://github.com/qwertyquerty/nascent/releases/tag/" + tag}, {"assets", files}};
}

static nlohmann::json complete_release(const std::string& tag) {
    return release(tag, {update_package_name(tag, "win", "x86_64", UpdatePackage::ZIP), update_package_name(tag, "linux", "x86_64", UpdatePackage::APPIMAGE), CHECKSUMS_FILE_NAME});
}

NASCENT_TEST(release_versions_parse) {
    ReleaseVersion stable = version("1.2.3");
    CHECK(stable.major == 1 && stable.minor == 2 && stable.patch == 3 && !stable.prerelease);

    ReleaseVersion indev = version("0.1.0.indev10");
    CHECK(indev.prerelease && indev.label == "indev" && indev.label_number == 10);

    CHECK(!parse_release_version("0.0.0-dev"));
    CHECK(!parse_release_version("1.2"));
    CHECK(!parse_release_version("1.2.3."));
    CHECK(!parse_release_version("01.2.3"));
    CHECK(!parse_release_version("1.2.3.rc-1"));
    CHECK(!parse_release_version("v1.2.3"));
}

NASCENT_TEST(release_versions_order) {
    CHECK(version("0.1.0.indev9") < version("0.1.0.indev10"));
    CHECK(version("0.1.0.indev10") < version("0.1.0"));
    CHECK(version("0.1.0") < version("0.1.1.indev1"));
    CHECK(version("0.1.0.indev2") < version("0.1.0.rc1"));
    CHECK(version("0.9.9") < version("0.10.0"));
    CHECK(version("1.0.0.RC2") == version("1.0.0.rc2"));
}

NASCENT_TEST(newest_update_picks_the_highest_newer_release) {
    nlohmann::json releases = {complete_release("0.1.0.indev9"), complete_release("0.1.0.indev11"), complete_release("0.1.0.indev10"), release("0.1.0.indev12", {}, true)};
    std::optional<UpdateRelease> update = newest_update(releases, version("0.1.0.indev9"), "win", "x86_64", UpdatePackage::ZIP);

    CHECK(update && update->tag == "0.1.0.indev11");
    CHECK(update && update->package.name == "nascent-0.1.0.indev11-win-x86_64.zip");
    CHECK(update && update->checksums.name == CHECKSUMS_FILE_NAME);
    CHECK(update && update->page_url.ends_with("/0.1.0.indev11"));
    CHECK(!newest_update(releases, version("0.1.0.indev11"), "win", "x86_64", UpdatePackage::ZIP));
}

NASCENT_TEST(newest_update_needs_a_matching_package_and_checksums) {
    nlohmann::json releases = {complete_release("0.2.0"), release("0.3.0", {update_package_name("0.3.0", "win", "x86_64", UpdatePackage::ZIP)})};

    CHECK(newest_update(releases, version("0.1.0"), "win", "x86_64", UpdatePackage::ZIP)->tag == "0.2.0");
    CHECK(newest_update(releases, version("0.1.0"), "linux", "x86_64", UpdatePackage::APPIMAGE)->tag == "0.2.0");
    CHECK(!newest_update(releases, version("0.1.0"), "mac", "arm64", UpdatePackage::ZIP));
    CHECK(!newest_update(releases, version("0.1.0"), "linux", "x86_64", UpdatePackage::ZIP));
    CHECK(!newest_update(nlohmann::json::object(), version("0.1.0"), "win", "x86_64", UpdatePackage::ZIP));
}

NASCENT_TEST(stable_builds_skip_prereleases) {
    nlohmann::json releases = {complete_release("1.0.0"), complete_release("1.1.0.rc1")};

    CHECK(newest_update(releases, version("0.9.0"), "win", "x86_64", UpdatePackage::ZIP)->tag == "1.0.0");
    CHECK(!newest_update(releases, version("1.0.0"), "win", "x86_64", UpdatePackage::ZIP));
    CHECK(newest_update(releases, version("1.0.0.rc1"), "win", "x86_64", UpdatePackage::ZIP)->tag == "1.1.0.rc1");
}

NASCENT_TEST(checksums_are_found_by_file_name) {
    std::string hash(64, 'a');
    std::string other(64, 'B');
    std::string sums = other + "  nascent-1.0.0-win-x86_64.zip.old\r\n" + hash + " *nascent-1.0.0-win-x86_64.zip\n" + other + "  SHA256SUMS\n";

    CHECK(checksum_for(sums, "nascent-1.0.0-win-x86_64.zip") == hash);
    CHECK(checksum_for(sums, "SHA256SUMS") == std::string(64, 'b'));
    CHECK(!checksum_for(sums, "nascent-1.0.0-linux-x86_64.zip"));
    CHECK(!checksum_for(std::string(63, 'a') + "  x.zip", "x.zip"));
    CHECK(!checksum_for(std::string(64, 'g') + "  x.zip", "x.zip"));
}

NASCENT_TEST(update_package_names_per_product) {
    CHECK(update_package_name("1.2.0", "linux", "x86_64", UpdatePackage::APPIMAGE) == "nascent-1.2.0-linux-x86_64.AppImage");
    CHECK(update_package_name("1.2.0", "linux", "arm64", UpdatePackage::APPIMAGE, "nascent-editor") == "nascent-editor-1.2.0-linux-arm64.AppImage");

    nlohmann::json releases = nlohmann::json::array({release("1.2.0", {update_package_name("1.2.0", "linux", "x86_64", UpdatePackage::APPIMAGE), CHECKSUMS_FILE_NAME})});
    ReleaseVersion current = version("1.0.0");
    CHECK(newest_update(releases, current, "linux", "x86_64", UpdatePackage::APPIMAGE).has_value());
    CHECK(!newest_update(releases, current, "linux", "x86_64", UpdatePackage::APPIMAGE, "nascent-editor").has_value());
}
