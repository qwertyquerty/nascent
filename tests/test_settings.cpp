#include "SettingsStore.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace nascent;
using namespace nascent::test;

struct TestSettings {
    int32_t speed = 100;
    std::string name = "default";
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TestSettings, speed, name)

static std::filesystem::path settings_file(const std::string& name, const std::string& text) {
    std::filesystem::path directory = std::filesystem::temp_directory_path() / "nascent_settings_tests";
    std::filesystem::create_directories(directory);
    std::filesystem::path path = directory / (name + ".json");
    std::filesystem::remove(directory / (name + ".invalid.json"));
    std::ofstream(path, std::ios::trunc) << text;
    return path;
}

static nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    return nlohmann::json::parse(ifs, nullptr, false);
}

NASCENT_TEST(settings) {
    std::string error;
    std::map<uint32_t, VersionedJsonFile::Migration> migrations = {
        {1, [](nlohmann::json& data) {
             data["speed"] = data["old_speed"].get<int32_t>() * 2;
             data.erase("old_speed");
         }}
    };

    std::filesystem::path migrated = settings_file("migrate", R"({"version": 1, "old_speed": 21, "future": "kept"})");
    SettingsStore<TestSettings> store(migrated, 2, migrations);
    CHECK(store.load(error) == SettingsLoadStatus::MIGRATED);
    CHECK(store.value.speed == 42);
    CHECK(read_json(migrated)["version"] == 2);
    CHECK(read_json(migrated)["future"] == "kept");

    std::filesystem::path newer = settings_file("newer", R"({"version": 9, "speed": 77})");
    SettingsStore<TestSettings> newer_store(newer, 1);
    CHECK(newer_store.load(error) == SettingsLoadStatus::NEWER_VERSION);
    CHECK(!newer_store.save(error));
    CHECK(read_json(newer)["version"] == 9);

    std::filesystem::path invalid = settings_file("invalid", "not json {");
    SettingsStore<TestSettings> invalid_store(invalid, 1);
    CHECK(invalid_store.load(error) == SettingsLoadStatus::INVALID);
    CHECK(std::filesystem::exists(invalid_store.get_file().get_backup_path()));
    CHECK(read_json(invalid)["speed"] == 100);
}
