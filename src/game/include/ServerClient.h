#pragma once

#include "json.h"

#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace nascent {
    struct ServerResponse {
        long status = 0;
        nlohmann::json body;
        std::string error;

        bool ok() const;
        std::string error_code() const;
        std::string message() const;
    };

    template <typename F>
    auto run_detached(F&& work) -> std::future<decltype(work())> {
        using Result = decltype(work());
        auto promise = std::make_shared<std::promise<Result>>();
        std::future<Result> future = promise->get_future();

        std::thread([promise, work = std::forward<F>(work)]() mutable {
            promise->set_value(work());
        }).detach();

        return future;
    }

    class ServerClient {
        public:
        static ServerResponse request(const std::string& base_url, const std::string& method, const std::string& path, const nlohmann::json& body = nullptr, const std::string& access_token = "");
        static std::future<ServerResponse> request_async(const std::string& base_url, const std::string& method, const std::string& path, const nlohmann::json& body = nullptr, const std::string& access_token = "");
        static std::string url_encode(const std::string& text);
    };
}
