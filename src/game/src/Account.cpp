#include "Account.h"

#include "Platform.h"
#include "SettingsManager.h"
#include "SettingsStore.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>

namespace nascent {
    struct AccountFile {
        std::string server_url;
        std::string username;
        std::string refresh_token;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AccountFile, server_url, username, refresh_token)

    namespace {
        std::mutex state_mutex;
        std::mutex refresh_mutex;
        std::unique_ptr<SettingsStore<AccountFile>> store;
        AccountFile session;
        std::string access;

        std::filesystem::path account_path() {
            return user_config_directory(SettingsManager::APP_NAME) / Account::FILE_NAME;
        }

        void save_locked() {
            if (store == nullptr) {
                store = std::make_unique<SettingsStore<AccountFile>>(account_path(), Account::FILE_VERSION);
            }

            store->value = session;
            std::string error;

            if (!store->save(error)) {
                std::cerr << "Account: " << error << std::endl;
                return;
            }

            std::error_code ec;
            std::filesystem::permissions(account_path(), std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perm_options::replace, ec);
        }

        void clear_locked() {
            std::string server_url = session.server_url;
            session = AccountFile();
            session.server_url = server_url;
            access.clear();
            save_locked();
        }

        bool apply_locked(const ServerResponse& response) {
            const nlohmann::json& tokens = response.body.is_object() ? response.body.value("tokens", nlohmann::json()) : nlohmann::json();

            if (!tokens.is_object() || !tokens.contains("access_token") || !tokens.contains("refresh_token")) {
                return false;
            }

            access = tokens["access_token"].get<std::string>();
            session.refresh_token = tokens["refresh_token"].get<std::string>();

            if (response.body.contains("user") && response.body["user"].is_object()) {
                session.username = response.body["user"].value("username", session.username);
            }

            save_locked();
            return true;
        }

        bool refresh(const std::string& stale_access) {
            std::lock_guard<std::mutex> refreshing(refresh_mutex);
            std::string token;
            std::string server_url;

            {
                std::lock_guard<std::mutex> lock(state_mutex);

                if (!access.empty() && access != stale_access) {
                    return true;
                }

                token = session.refresh_token;
                server_url = session.server_url;
            }

            if (token.empty()) {
                return false;
            }

            ServerResponse response = ServerClient::request(server_url, "POST", "/auth/refresh", {{"refresh_token", token}});
            std::lock_guard<std::mutex> lock(state_mutex);

            if (session.refresh_token != token) {
                return !access.empty();
            }

            if (response.ok()) {
                return apply_locked(response);
            }

            if (response.status == 401) {
                clear_locked();
            }

            return false;
        }

        std::future<ServerResponse> authenticate(const std::string& path, const nlohmann::json& body) {
            std::string server_url = SettingsManager::settings.server_url;

            return run_detached([server_url, path, body] {
                ServerResponse response = ServerClient::request(server_url, "POST", path, body);

                if (response.ok()) {
                    std::lock_guard<std::mutex> lock(state_mutex);
                    session.server_url = server_url;

                    if (!apply_locked(response)) {
                        response.error = "the server sent an unexpected response";
                    }
                }

                return response;
            });
        }

        ServerResponse not_signed_in() {
            ServerResponse response;
            response.status = 401;
            response.body = {{"error", {{"code", "not_signed_in"}, {"message", "Sign in to do this"}}}};
            return response;
        }
    }

    void Account::load() {
        std::lock_guard<std::mutex> lock(state_mutex);
        store = std::make_unique<SettingsStore<AccountFile>>(account_path(), FILE_VERSION);

        std::string error;
        store->load(error);

        if (!error.empty()) {
            std::cerr << "Account: " << error << std::endl;
        }

        session = store->value;
        session.server_url = server_host(session.server_url);

        if (session.server_url != SettingsManager::settings.server_url) {
            session = AccountFile();
            session.server_url = SettingsManager::settings.server_url;
            save_locked();
            return;
        }

        if (!session.refresh_token.empty()) {
            run_detached([] { return refresh(""); });
        }
    }

    bool Account::signed_in() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return !session.refresh_token.empty();
    }

    std::string Account::username() {
        std::lock_guard<std::mutex> lock(state_mutex);
        return session.username;
    }

    std::future<ServerResponse> Account::sign_in(const std::string& login, const std::string& password) {
        return authenticate("/auth/login", {{"login", login}, {"password", password}});
    }

    std::future<ServerResponse> Account::create(const std::string& username, const std::string& email, const std::string& password) {
        return authenticate("/auth/register", {{"username", username}, {"email", email}, {"password", password}});
    }

    void Account::sign_out() {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (!access.empty()) {
            ServerClient::request_async(session.server_url, "POST", "/auth/logout", nullptr, access);
        }

        clear_locked();
    }

    void Account::use_server(const std::string& server_url) {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (session.server_url == server_url) {
            return;
        }

        if (!access.empty()) {
            ServerClient::request_async(session.server_url, "POST", "/auth/logout", nullptr, access);
        }

        session = AccountFile();
        session.server_url = server_url;
        access.clear();
        save_locked();
    }

    ServerResponse Account::authorized(const std::string& method, const std::string& path, const nlohmann::json& body) {
        std::string token;
        std::string server_url;

        {
            std::lock_guard<std::mutex> lock(state_mutex);

            if (session.refresh_token.empty()) {
                return not_signed_in();
            }

            token = access;
            server_url = session.server_url;
        }

        if (token.empty()) {
            if (!refresh(token)) {
                return not_signed_in();
            }

            std::lock_guard<std::mutex> lock(state_mutex);
            token = access;
        }

        ServerResponse response = ServerClient::request(server_url, method, path, body, token);

        if (response.status == 401 && response.error_code() == "invalid_token" && refresh(token)) {
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                token = access;
            }

            response = ServerClient::request(server_url, method, path, body, token);
        }

        return response;
    }
}
