#pragma once

#include "ServerClient.h"

#include <cstdint>
#include <future>
#include <string>

namespace nascent {
    class Account {
        public:
        static constexpr const char* FILE_NAME = "account.json";
        static constexpr uint32_t FILE_VERSION = 1;

        static void load();

        static bool signed_in();
        static std::string username();

        static std::future<ServerResponse> sign_in(const std::string& login, const std::string& password);
        static std::future<ServerResponse> create(const std::string& username, const std::string& email, const std::string& password);
        static void sign_out();
        static void use_server(const std::string& server_url);

        static ServerResponse authorized(const std::string& method, const std::string& path, const nlohmann::json& body = nullptr);
    };
}
