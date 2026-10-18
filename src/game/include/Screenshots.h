#pragma once

#include "gse/render/FrameCapture.h"

#include <filesystem>
#include <string>

namespace nascent {
    struct ScreenshotResult {
        bool ok = false;
        std::filesystem::path path;
        std::string error;
    };

    class Screenshots {
        public:
        static constexpr const char* DIRECTORY = "screenshots";

        static std::filesystem::path directory();
        static ScreenshotResult save(const CapturedFrame& frame);
    };
}
