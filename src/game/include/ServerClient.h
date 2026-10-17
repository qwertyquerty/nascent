#pragma once

#include "gse/concurrency/Async.h"
#include "json.h"

#include <atomic>
#include <functional>
#include <future>
#include <optional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace nascent {
    struct ServerResponse {
        long status = 0;
        double latency_ms = 0;
        nlohmann::json body;
        std::string error;

        bool ok() const;
        std::string error_code() const;
        std::string message() const;
    };

    std::string server_host(const std::string& address);
    std::string server_base_url(const std::string& address);

    class ServerClient {
        public:
        static ServerResponse request(const std::string& server, const std::string& method, const std::string& path, const nlohmann::json& body = nullptr, const std::string& access_token = "");
        static std::future<ServerResponse> request_async(const std::string& server, const std::string& method, const std::string& path, const nlohmann::json& body = nullptr, const std::string& access_token = "");
        static std::string url_encode(const std::string& text);
        static std::optional<std::string> download(const std::string& url, size_t max_bytes, const std::function<void(double)>& progress, std::string& error, bool& retryable, const std::atomic<bool>* cancelled = nullptr);
    };
}
