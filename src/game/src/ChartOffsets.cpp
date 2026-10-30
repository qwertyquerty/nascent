#include "ChartOffsets.h"

#include "gse/Platform.h"
#include "SettingsManager.h"
#include "SettingsStore.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <unordered_map>

namespace nascent {
    struct ChartOffsetsFile {
        std::unordered_map<std::string, int32_t> charts;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChartOffsetsFile, charts)

    static std::unique_ptr<SettingsStore<ChartOffsetsFile>> store;

    void ChartOffsets::load() {
        store = std::make_unique<SettingsStore<ChartOffsetsFile>>(user_config_directory(SettingsManager::APP_NAME) / FILE_NAME, FILE_VERSION);
        std::string error;
        store->load(error);

        if (!error.empty()) {
            std::cerr << "Chart offsets: " << error << std::endl;
        }
    }

    int32_t ChartOffsets::get(const std::string& chart_hash) {
        if (store == nullptr) {
            return 0;
        }

        auto found = store->value.charts.find(chart_hash);
        return found != store->value.charts.end() ? std::clamp(found->second, -LIMIT_MS, LIMIT_MS) : 0;
    }

    int32_t ChartOffsets::set(const std::string& chart_hash, int32_t offset) {
        offset = std::clamp(offset, -LIMIT_MS, LIMIT_MS);

        if (store == nullptr || chart_hash.empty()) {
            return offset;
        }

        if (offset == 0) {
            store->value.charts.erase(chart_hash);
        }
        else {
            store->value.charts[chart_hash] = offset;
        }

        std::string error;

        if (!store->save(error)) {
            std::cerr << "Chart offsets: " << error << std::endl;
        }

        return offset;
    }
}
