#pragma once

#include "ServerApi.h"

#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace nascent {
    class Chart;
    struct Replay;

    struct SubmissionNotice {
        std::string message;
        bool is_error = false;
    };

    class SubmissionQueue {
        public:
        static constexpr const char* DIRECTORY = "pending_submissions";
        static constexpr double FIRST_RETRY_SECONDS = 5.0;
        static constexpr double MAX_RETRY_SECONDS = 600.0;
        static constexpr double GIVE_UP_SECONDS = 30.0 * 24 * 60 * 60;
        static constexpr uint32_t FILE_VERSION = 1;

        static std::filesystem::path directory();
        static double retry_delay_seconds(uint32_t attempts);

        static void start();
        static void stop();
        static std::future<ApiResult<Submission>> submit(const Chart& chart, Replay replay, std::shared_future<ApiResult<PlayTicket>> ticket, std::filesystem::path replay_directory);
        static std::vector<SubmissionNotice> take_notices();
    };
}
