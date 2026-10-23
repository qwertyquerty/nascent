#include "Favorites.h"

#include "gse/Platform.h"
#include "SettingsManager.h"
#include "SettingsStore.h"

#include <iostream>
#include <memory>
#include <unordered_set>

namespace nascent {
    struct FavoritesFile {
        std::unordered_set<std::string> charts;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FavoritesFile, charts)

    static std::unique_ptr<SettingsStore<FavoritesFile>> store;

    void Favorites::load() {
        store = std::make_unique<SettingsStore<FavoritesFile>>(user_config_directory(SettingsManager::APP_NAME) / FILE_NAME, FILE_VERSION);
        std::string error;
        store->load(error);

        if (!error.empty()) {
            std::cerr << "Favorites: " << error << std::endl;
        }
    }

    bool Favorites::contains(const std::string& chart_hash) {
        return store != nullptr && store->value.charts.contains(chart_hash);
    }

    bool Favorites::toggle(const std::string& chart_hash) {
        if (store == nullptr) {
            return false;
        }

        bool added = store->value.charts.insert(chart_hash).second;

        if (!added) {
            store->value.charts.erase(chart_hash);
        }

        std::string error;

        if (!store->save(error)) {
            std::cerr << "Favorites: " << error << std::endl;
        }

        return added;
    }
}
