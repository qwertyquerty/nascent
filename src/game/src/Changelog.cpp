#include "Changelog.h"

#include "ReleaseUpdate.h"
#include "SettingsManager.h"

#include <fstream>
#include <optional>

namespace nascent {
    namespace {
        std::string text_of(const nlohmann::json& object, const char* key) {
            return object.contains(key) && object[key].is_string() ? object[key].get<std::string>() : std::string();
        }
    }

    std::vector<ChangelogRelease> parse_changelog(const nlohmann::json& changelog) {
        std::vector<ChangelogRelease> releases;

        if (!changelog.is_object() || !changelog.contains("releases") || !changelog["releases"].is_array()) {
            return releases;
        }

        for (const nlohmann::json& entry : changelog["releases"]) {
            if (!entry.is_object() || text_of(entry, "version").empty()) {
                continue;
            }

            ChangelogRelease release{text_of(entry, "version"), {}};

            if (entry.contains("sections") && entry["sections"].is_array()) {
                for (const nlohmann::json& part : entry["sections"]) {
                    if (!part.is_object() || !part.contains("notes") || !part["notes"].is_array()) {
                        continue;
                    }

                    ChangelogSection section{text_of(part, "title"), {}};

                    for (const nlohmann::json& note : part["notes"]) {
                        if (note.is_string()) {
                            section.notes.push_back(note.get<std::string>());
                        }
                    }

                    release.sections.push_back(std::move(section));
                }
            }

            releases.push_back(std::move(release));
        }

        return releases;
    }

    std::vector<ChangelogRelease> Changelog::load() {
        std::ifstream file(PATH, std::ios::binary);
        return parse_changelog(nlohmann::json::parse(file, nullptr, false));
    }

    std::vector<ChangelogRelease> Changelog::since_last_run() {
        std::optional<ReleaseVersion> current = parse_release_version(NASCENT_VERSION_STRING);

        if (!current) {
            return {};
        }

        std::string& seen = SettingsManager::settings.last_run_version;
        std::optional<ReleaseVersion> previous = parse_release_version(seen);
        bool updated = SettingsManager::load_status != SettingsLoadStatus::CREATED && (!previous || *previous < *current);
        std::vector<ChangelogRelease> shown;

        if (updated) {
            for (ChangelogRelease& release : load()) {
                std::optional<ReleaseVersion> version = parse_release_version(release.version);
                bool newer = version && *version <= *current && (previous ? *previous < *version : *version == *current);

                if (newer) {
                    shown.push_back(std::move(release));
                }
            }
        }

        if (seen != NASCENT_VERSION_STRING) {
            seen = NASCENT_VERSION_STRING;
            SettingsManager::save();
        }

        return shown;
    }
}
