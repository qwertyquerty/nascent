#include "ServerClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

namespace nascent {
    static constexpr long REQUEST_TIMEOUT_S = 20;
    static constexpr long CONNECT_TIMEOUT_S = 10;
    static constexpr long DOWNLOAD_LOW_SPEED_BYTES = 1024;
    static constexpr long DOWNLOAD_LOW_SPEED_TIME_S = 30;

    struct CurlShare {
        CURLSH* handle = nullptr;
        std::mutex locks[CURL_LOCK_DATA_LAST];
    };

    static void lock_share(CURL*, curl_lock_data data, curl_lock_access, void* shared) {
        static_cast<CurlShare*>(shared)->locks[data].lock();
    }

    static void unlock_share(CURL*, curl_lock_data data, void* shared) {
        static_cast<CurlShare*>(shared)->locks[data].unlock();
    }

    static CURLSH* curl_share() {
        static CurlShare* shared = [] {
            curl_global_init(CURL_GLOBAL_DEFAULT);

            CurlShare* created = new CurlShare();
            created->handle = curl_share_init();
            curl_share_setopt(created->handle, CURLSHOPT_LOCKFUNC, lock_share);
            curl_share_setopt(created->handle, CURLSHOPT_UNLOCKFUNC, unlock_share);
            curl_share_setopt(created->handle, CURLSHOPT_USERDATA, created);
            curl_share_setopt(created->handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
            curl_share_setopt(created->handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
            return created;
        }();

        return shared->handle;
    }

    static void configure_handle(CURL* curl) {
        curl_easy_setopt(curl, CURLOPT_SHARE, curl_share());
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nascent");
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
#ifdef _WIN32
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif
    }

    class PooledHandle {
        static constexpr size_t MAX_IDLE = 4;

        struct Pool {
            std::mutex mutex;
            std::vector<CURL*> idle;
        };

        static Pool& pool() {
            static Pool* shared = new Pool();
            return *shared;
        }

        CURL* curl = nullptr;

        public:
        PooledHandle() {
            curl_share();

            {
                std::lock_guard<std::mutex> lock(pool().mutex);

                if (!pool().idle.empty()) {
                    curl = pool().idle.back();
                    pool().idle.pop_back();
                }
            }

            if (curl == nullptr) {
                curl = curl_easy_init();
            }

            if (curl != nullptr) {
                configure_handle(curl);
            }
        }

        ~PooledHandle() {
            if (curl == nullptr) {
                return;
            }

            curl_easy_reset(curl);

            {
                std::lock_guard<std::mutex> lock(pool().mutex);

                if (pool().idle.size() < MAX_IDLE) {
                    pool().idle.push_back(curl);
                    return;
                }
            }

            curl_easy_cleanup(curl);
        }

        PooledHandle(const PooledHandle&) = delete;
        PooledHandle& operator=(const PooledHandle&) = delete;

        CURL* get() const {
            return curl;
        }
    };

    static bool is_https(const std::string& url) {
        static constexpr std::string_view scheme = "https://";

        if (url.size() < scheme.size()) {
            return false;
        }

        for (size_t i = 0; i < scheme.size(); i++) {
            if (std::tolower((unsigned char)url[i]) != scheme[i]) {
                return false;
            }
        }

        return true;
    }

    static size_t append_body(char* data, size_t size, size_t count, void* target) {
        static_cast<std::string*>(target)->append(data, size * count);
        return size * count;
    }

    bool ServerResponse::ok() const {
        return error.empty() && status >= 200 && status < 300;
    }

    std::string ServerResponse::error_code() const {
        if (body.is_object() && body.contains("error") && body["error"].is_object()) {
            return body["error"].value("code", std::string());
        }

        return std::string();
    }

    static constexpr const char* GENERIC_VALIDATION_MESSAGE = "The request body is invalid";

    std::string ServerResponse::message() const {
        if (!error.empty()) {
            return std::format("could not reach the server: {}", error);
        }

        if (body.is_object() && body.contains("error") && body["error"].is_object()) {
            const nlohmann::json& problem = body["error"];
            const nlohmann::json& details = problem.value("details", nlohmann::json());
            std::string summary = problem.value("message", std::string());

            if (problem.value("code", std::string()) == "validation_error" && !summary.empty() && summary != GENERIC_VALIDATION_MESSAGE) {
                return summary;
            }

            if (details.is_array() && !details.empty() && details[0].is_object() && details[0].contains("message")) {
                return std::format("{}: {}", details[0].value("field", std::string("request")), details[0]["message"].get<std::string>());
            }

            if (problem.contains("message") && problem["message"].is_string()) {
                return problem["message"].get<std::string>();
            }
        }

        return std::format("the server answered with HTTP {}", status);
    }

    std::string server_host(const std::string& address) {
        size_t start = address.find_first_not_of(" \t");
        size_t end = address.find_last_not_of(" \t");
        std::string host = start == std::string::npos ? std::string() : address.substr(start, end - start + 1);

        if (size_t scheme = host.find("://"); scheme != std::string::npos) {
            host.erase(0, scheme + 3);
        }

        while (host.ends_with('/')) {
            host.pop_back();
        }

        if (host.ends_with("/api/v1")) {
            host.erase(host.size() - std::string_view("/api/v1").size());
        }

        while (host.ends_with('/')) {
            host.pop_back();
        }

        return host;
    }

    std::string server_base_url(const std::string& address) {
        return "https://" + server_host(address);
    }

    ServerResponse ServerClient::request(const std::string& server, const std::string& method, const std::string& path, const nlohmann::json& body, const std::string& access_token) {
        ServerResponse response;
        std::string host = server_host(server);

        if (host.empty()) {
            response.error = "no server address is set";
            return response;
        }

        PooledHandle handle;
        CURL* curl = handle.get();

        if (curl == nullptr) {
            response.error = "could not start a request";
            return response;
        }

        std::string url = "https://" + host + "/api/v1" + path;
        std::string payload = body.is_null() ? std::string() : body.dump();
        std::string authorization = "Authorization: Bearer " + access_token;
        std::string received;
        char error_buffer[CURL_ERROR_SIZE] = {};

        curl_slist* headers = curl_slist_append(nullptr, "Accept: application/json");
        headers = curl_slist_append(headers, "Content-Type: application/json");

        if (!access_token.empty()) {
            headers = curl_slist_append(headers, authorization.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_body);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &received);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, REQUEST_TIMEOUT_S);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);

        if (method != "GET") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payload.size());
        }

        CURLcode result = curl_easy_perform(curl);

        if (result != CURLE_OK) {
            response.error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(result);
        }
        else {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
            curl_off_t sent_us = 0;
            curl_off_t first_byte_us = 0;
            curl_easy_getinfo(curl, CURLINFO_PRETRANSFER_TIME_T, &sent_us);
            curl_easy_getinfo(curl, CURLINFO_STARTTRANSFER_TIME_T, &first_byte_us);
            response.latency_ms = (double)std::max<curl_off_t>(first_byte_us - sent_us, 0) / 1000.0;
            response.body = nlohmann::json::parse(received, nullptr, false);

            if (response.body.is_discarded()) {
                response.body = nullptr;
            }
        }

        curl_easy_reset(curl);
        curl_slist_free_all(headers);
        return response;
    }

    std::future<ServerResponse> ServerClient::request_async(const std::string& server, const std::string& method, const std::string& path, const nlohmann::json& body, const std::string& access_token) {
        return run_detached([server, method, path, body, access_token] {
            return request(server, method, path, body, access_token);
        });
    }

    struct DownloadTarget {
        std::string data;
        size_t max_bytes = 0;
        const std::function<void(double)>* progress = nullptr;
        const std::atomic<bool>* cancelled = nullptr;
    };

    static size_t append_download(char* data, size_t size, size_t count, void* target) {
        DownloadTarget* download = static_cast<DownloadTarget*>(target);

        if (download->data.size() + size * count > download->max_bytes) {
            return 0;
        }

        download->data.append(data, size * count);
        return size * count;
    }

    static int report_progress(void* target, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
        DownloadTarget* download = static_cast<DownloadTarget*>(target);

        if (download->cancelled != nullptr && *download->cancelled) {
            return 1;
        }

        if (total > 0 && *download->progress) {
            (*download->progress)((double)now / (double)total);
        }

        return 0;
    }

    std::optional<std::string> ServerClient::download(const std::string& url, size_t max_bytes, const std::function<void(double)>& progress, std::string& error, bool& retryable, const std::atomic<bool>* cancelled) {
        retryable = false;

        if (!is_https(url)) {
            error = "download addresses must start with https://";
            return std::nullopt;
        }

        PooledHandle handle;
        CURL* curl = handle.get();

        if (curl == nullptr) {
            error = "could not start a download";
            return std::nullopt;
        }

        DownloadTarget target{std::string(), max_bytes, &progress, cancelled};
        char error_buffer[CURL_ERROR_SIZE] = {};
        long status = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_download);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &target);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, report_progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &target);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CONNECT_TIMEOUT_S);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, DOWNLOAD_LOW_SPEED_BYTES);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, DOWNLOAD_LOW_SPEED_TIME_S);

        CURLcode result = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_reset(curl);

        if (result == CURLE_WRITE_ERROR && target.data.size() + CURL_MAX_WRITE_SIZE > max_bytes) {
            error = std::format("the download is larger than {} MB", max_bytes / (1024 * 1024));
            return std::nullopt;
        }

        if (result == CURLE_ABORTED_BY_CALLBACK) {
            error = "the download was cancelled";
            return std::nullopt;
        }

        if (result != CURLE_OK) {
            error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(result);
            retryable = result != CURLE_WRITE_ERROR && result != CURLE_URL_MALFORMAT && result != CURLE_UNSUPPORTED_PROTOCOL && result != CURLE_PARTIAL_FILE;
            return std::nullopt;
        }

        if (status >= 400) {
            error = std::format("the download server answered with HTTP {}", status);
            retryable = status == 429 || status >= 500;
            return std::nullopt;
        }

        return std::move(target.data);
    }

    std::string ServerClient::url_encode(const std::string& text) {
        static constexpr const char* HEX = "0123456789ABCDEF";
        std::string encoded;

        for (unsigned char c : text) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded.push_back((char)c);
            }
            else {
                encoded.push_back('%');
                encoded.push_back(HEX[c >> 4]);
                encoded.push_back(HEX[c & 0x0f]);
            }
        }

        return encoded;
    }
}
