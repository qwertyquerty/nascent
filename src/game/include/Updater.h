#pragma once

#include "ReleaseUpdate.h"

#include <cstddef>
#include <optional>
#include <string>

namespace nascent {
    enum class UpdateStatus {
        DISABLED,
        CHECKING,
        UP_TO_DATE,
        AVAILABLE,
        DOWNLOADING,
        READY,
        FAILED,
    };

    struct UpdateSnapshot {
        UpdateStatus status = UpdateStatus::DISABLED;
        std::optional<UpdateRelease> release;
        double progress = 0;
        std::string error;
        bool installable = false;
    };

    struct UpdateNotice {
        std::string message;
        bool is_error = false;
    };

    class Updater {
        public:
        static constexpr const char* REPOSITORY = "qwertyquerty/nascent";
        static constexpr const char* RESULT_FILE_NAME = "update_result.json";
        static constexpr size_t MAX_RELEASE_LIST_BYTES = 8 * 1024 * 1024;
        static constexpr size_t MAX_CHECKSUMS_BYTES = 64 * 1024;
        static constexpr size_t MAX_PACKAGE_BYTES = 1024 * 1024 * 1024;
        static constexpr int RELEASES_PER_PAGE = 100;

        static void recover();
        static void check();
        static UpdateSnapshot snapshot();
        static void start();
        static bool ready();
        static void install_and_relaunch();
        static std::optional<UpdateNotice> take_notice();
    };
}
