#pragma once

#include "json.h"

#include <string>
#include <vector>

namespace nascent {
    struct ChangelogSection {
        std::string title;
        std::vector<std::string> notes;
    };

    struct ChangelogRelease {
        std::string version;
        std::vector<ChangelogSection> sections;
    };

    std::vector<ChangelogRelease> parse_changelog(const nlohmann::json& changelog);

    class Changelog {
        public:
        static constexpr const char* PATH = "assets/changelog.json";

        static std::vector<ChangelogRelease> load();
        static std::vector<ChangelogRelease> since_last_run();
    };
}
