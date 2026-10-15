#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

namespace nascent {
    bool spawn_detached(const std::filesystem::path& program, const std::vector<std::string>& arguments) {
        std::string path = program.string();
        std::vector<std::string> owned = {path};
        owned.insert(owned.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;

        for (std::string& argument : owned) {
            argv.push_back(argument.data());
        }

        argv.push_back(nullptr);
        pid_t pid = 0;
        return posix_spawn(&pid, path.c_str(), nullptr, nullptr, argv.data(), environ) == 0;
    }

    void attach_parent_console() {
    }

    void precise_sleep(double seconds) {
        if (seconds > 0) {
            std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
        }
    }

    bool copy_to_clipboard(const std::string& text) {
        auto previous_handler = std::signal(SIGPIPE, SIG_IGN);
        bool copied = false;

        for (const char* command : platform_detail::clipboard_commands()) {
            FILE* pipe = popen(command, "w");

            if (pipe == nullptr) {
                continue;
            }

            bool written = std::fwrite(text.data(), 1, text.size(), pipe) == text.size();

            if (pclose(pipe) == 0 && written) {
                copied = true;
                break;
            }
        }

        std::signal(SIGPIPE, previous_handler);
        return copied;
    }

    bool open_url(const std::string& url) {
        if (!url.starts_with("https://")) {
            return false;
        }

        std::string program = platform_detail::url_opener();
        std::string argument = url;
        char* arguments[] = {program.data(), argument.data(), nullptr};
        pid_t pid = 0;

        if (posix_spawnp(&pid, program.c_str(), nullptr, nullptr, arguments, environ) != 0) {
            return false;
        }

        std::thread([pid] {
            waitpid(pid, nullptr, 0);
        }).detach();
        return true;
    }
}
