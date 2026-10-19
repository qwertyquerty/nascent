#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <execinfo.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

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

    bool attach_parent_console() {
        struct stat info;

        if (isatty(STDERR_FILENO)) {
            return true;
        }

        return fstat(STDERR_FILENO, &info) == 0 && (S_ISFIFO(info.st_mode) || S_ISREG(info.st_mode));
    }

    void redirect_output_to_log(const std::filesystem::path& file) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::filesystem::path previous = file;
        previous.replace_extension(".previous.log");
        std::filesystem::rename(file, previous, ec);

        if (std::freopen(file.c_str(), "w", stderr) == nullptr) {
            return;
        }

        std::setvbuf(stderr, nullptr, _IONBF, 0);

        if (std::freopen(file.c_str(), "a", stdout) != nullptr) {
            std::setvbuf(stdout, nullptr, _IONBF, 0);
        }
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

    static bool run_opener(std::string argument) {
        std::string program = platform_detail::url_opener();
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

    bool open_url(const std::string& url) {
        return url.starts_with("https://") && run_opener(url);
    }

    bool open_folder(const std::filesystem::path& folder) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        return run_opener(folder.string());
    }

    bool open_file(const std::filesystem::path& file) {
        std::error_code ec;
        return std::filesystem::is_regular_file(file, ec) && run_opener(file.string());
    }
}

namespace nascent::platform_detail {
    static constexpr int CRASH_STACK_FRAMES = 64;
    static int crash_output = STDERR_FILENO;

    static void write_text(const char* text) {
        ssize_t written = write(crash_output, text, std::strlen(text));
        (void)written;
    }

    static const char* signal_name(int signal) {
        switch (signal) {
            case SIGSEGV:
                return "SIGSEGV (invalid memory access)";
            case SIGABRT:
                return "SIGABRT (aborted)";
            case SIGFPE:
                return "SIGFPE (arithmetic error)";
            case SIGILL:
                return "SIGILL (illegal instruction)";
            case SIGBUS:
                return "SIGBUS (bus error)";
            default:
                return "unknown signal";
        }
    }

    static void crash_signal(int signal) {
        write_text("Crash: ");
        write_text(signal_name(signal));
        write_text("\nStack:\n");
        void* frames[CRASH_STACK_FRAMES] = {};
        int count = backtrace(frames, CRASH_STACK_FRAMES);
        backtrace_symbols_fd(frames, count, crash_output);
        std::signal(signal, SIG_DFL);
        std::raise(signal);
    }

    void install_crash_signals() {
        crash_output = fileno(stderr);

        for (int signal : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
            std::signal(signal, crash_signal);
        }
    }
}
