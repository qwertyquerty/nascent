#include "ServerClient.h"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <mutex>
#include <utility>

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
            curl_share_setopt(created->handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
            curl_share_setopt(created->handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
            curl_share_setopt(created->handle, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
            return created;
        }();

        return shared->handle;
    }

    static CURL* create_handle() {
        CURLSH* share = curl_share();
        CURL* curl = curl_easy_init();

        if (curl != nullptr) {
            curl_easy_setopt(curl, CURLOPT_SHARE, share);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "Nascent");
        }

        return curl;
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

    std::string ServerResponse::message() const {
        if (!error.empty()) {
            return std::format("could not reach the server: {}", error);
        }

        if (body.is_object() && body.contains("error") && body["error"].is_object()) {
            const nlohmann::json& problem = body["error"];
            const nlohmann::json& details = problem.value("details", nlohmann::json());

            if (details.is_array() && !details.empty() && details[0].is_object() && details[0].contains("message")) {
                return std::format("{}: {}", details[0].value("field", std::string("request")), details[0]["message"].get<std::string>());
            }

            if (problem.contains("message") && problem["message"].is_string()) {
                return problem["message"].get<std::string>();
            }
        }

        return std::format("the server answered with HTTP {}", status);
    }

    ServerResponse ServerClient::request(const std::string& base_url, const std::string& method, const std::string& path, const nlohmann::json& body, const std::string& access_token) {
        ServerResponse response;
        CURL* curl = create_handle();

        if (curl == nullptr) {
            response.error = "could not start a request";
            return response;
        }

        std::string url = base_url + "/api/v1" + path;
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

#ifdef _WIN32
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif

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

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    std::future<ServerResponse> ServerClient::request_async(const std::string& base_url, const std::string& method, const std::string& path, const nlohmann::json& body, const std::string& access_token) {
        return run_detached([base_url, method, path, body, access_token] {
            return request(base_url, method, path, body, access_token);
        });
    }

    struct DownloadTarget {
        std::string data;
        size_t max_bytes = 0;
        const std::function<void(double)>* progress = nullptr;
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

        if (total > 0 && *download->progress) {
            (*download->progress)((double)now / (double)total);
        }

        return 0;
    }

    std::optional<std::string> ServerClient::download(const std::string& url, size_t max_bytes, const std::function<void(double)>& progress, std::string& error, bool& retryable) {
        retryable = false;
        CURL* curl = create_handle();

        if (curl == nullptr) {
            error = "could not start a download";
            return std::nullopt;
        }

        DownloadTarget target{std::string(), max_bytes, &progress};
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

#ifdef _WIN32
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif

        CURLcode result = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);

        if (result == CURLE_WRITE_ERROR && target.data.size() + CURL_MAX_WRITE_SIZE > max_bytes) {
            error = std::format("the download is larger than {} MB", max_bytes / (1024 * 1024));
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
