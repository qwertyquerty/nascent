#include "SubmissionQueue.h"

#include "Account.h"
#include "Chart.h"
#include "gse/Platform.h"
#include "Replay.h"
#include "ServerClient.h"
#include "SettingsManager.h"
#include "SettingsStore.h"
#include "Sha256.h"
#include "Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace nascent {
    namespace {
        struct PendingSubmission {
            std::filesystem::path path;
            nlohmann::json body;
            nlohmann::json chart;
            std::string username;
            std::string name;
            uint32_t attempts = 0;
            int64_t created_at = 0;
            int64_t next_attempt_at = 0;
        };

        std::mutex mutex;
        std::condition_variable wake;
        std::thread worker;
        bool running = false;
        std::vector<SubmissionNotice> notices;
        std::atomic<int32_t> saving = 0;

        int64_t unix_seconds() {
            return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        }

        bool write(const PendingSubmission& pending) {
            nlohmann::json data = {
                {"body", pending.body},
                {"chart", pending.chart},
                {"username", pending.username},
                {"name", pending.name},
                {"attempts", pending.attempts},
                {"created_at", pending.created_at},
                {"next_attempt_at", pending.next_attempt_at},
            };
            std::string error;

            if (!VersionedJsonFile(pending.path, SubmissionQueue::FILE_VERSION).save(data, error)) {
                std::cerr << "Submissions: " << error << std::endl;
                return false;
            }

            return true;
        }

        std::optional<PendingSubmission> read(const std::filesystem::path& path) {
            nlohmann::json data;
            std::string error;

            if (VersionedJsonFile(path, SubmissionQueue::FILE_VERSION).load(data, error) != SettingsLoadStatus::LOADED || !data.is_object()) {
                return std::nullopt;
            }

            PendingSubmission pending;
            pending.path = path;
            pending.body = data.value("body", nlohmann::json());
            pending.chart = data.value("chart", nlohmann::json());
            pending.username = data.value("username", std::string());
            pending.name = data.value("name", std::string());
            pending.attempts = data.value("attempts", 0u);
            pending.created_at = data.value("created_at", (int64_t)0);
            pending.next_attempt_at = data.value("next_attempt_at", (int64_t)0);

            if (!pending.body.is_object()) {
                return std::nullopt;
            }

            return pending;
        }

        std::vector<PendingSubmission> load_all() {
            std::vector<PendingSubmission> pending;
            std::error_code ec;

            for (const auto& entry : std::filesystem::directory_iterator(SubmissionQueue::directory(), ec)) {
                if (entry.path().extension() != ".json") {
                    continue;
                }

                if (std::optional<PendingSubmission> loaded = read(entry.path())) {
                    pending.push_back(std::move(*loaded));
                }
            }

            return pending;
        }

        void notify(std::string message, bool is_error) {
            std::lock_guard<std::mutex> lock(mutex);
            notices.push_back({std::move(message), is_error});
        }

        void discard(const PendingSubmission& pending) {
            std::error_code ec;
            std::filesystem::remove(pending.path, ec);
        }

        void retry(PendingSubmission& pending) {
            ApiResult<Submission> result = ServerApi::send_submission(pending.body, pending.chart);

            if (result.ok()) {
                discard(pending);
                notify(std::format("Submitted {} after retrying", pending.name), false);
                return;
            }

            if (result.error_code() == "duplicate_replay") {
                discard(pending);
                return;
            }

            if (!ServerApi::is_retryable(result.response)) {
                discard(pending);
                notify(std::format("Could not submit {}: {}", pending.name, result.error()), true);
                return;
            }

            pending.attempts++;
            pending.next_attempt_at = unix_seconds() + (int64_t)std::ceil(SubmissionQueue::retry_delay_seconds(pending.attempts));
            write(pending);
        }

        void run() {
            std::unique_lock<std::mutex> lock(mutex);

            while (running) {
                lock.unlock();
                std::vector<PendingSubmission> pending = load_all();
                int64_t now = unix_seconds();
                std::optional<int64_t> next_due;
                std::string username = Account::signed_in() ? Account::username() : std::string();

                for (PendingSubmission& entry : pending) {
                    if (now - entry.created_at > (int64_t)SubmissionQueue::GIVE_UP_SECONDS) {
                        discard(entry);
                        notify(std::format("Gave up submitting {}", entry.name), true);
                        continue;
                    }

                    if (entry.username != username) {
                        continue;
                    }

                    if (entry.next_attempt_at <= now) {
                        retry(entry);
                    }

                    if (std::filesystem::exists(entry.path)) {
                        next_due = std::min(next_due.value_or(entry.next_attempt_at), entry.next_attempt_at);
                    }
                }

                lock.lock();

                if (!running) {
                    break;
                }

                int64_t wait = next_due ? std::clamp<int64_t>(*next_due - unix_seconds(), 1, (int64_t)SubmissionQueue::MAX_RETRY_SECONDS) : (int64_t)SubmissionQueue::MAX_RETRY_SECONDS;
                wake.wait_for(lock, std::chrono::seconds(wait));
            }
        }
    }

    std::filesystem::path SubmissionQueue::directory() {
        return user_config_directory(SettingsManager::APP_NAME) / DIRECTORY;
    }

    double SubmissionQueue::retry_delay_seconds(uint32_t attempts) {
        double delay = FIRST_RETRY_SECONDS * std::pow(2.0, (double)std::max<uint32_t>(attempts, 1) - 1.0);
        return std::min(delay, MAX_RETRY_SECONDS);
    }

    void SubmissionQueue::start() {
        std::lock_guard<std::mutex> lock(mutex);

        if (running) {
            return;
        }

        running = true;
        worker = std::thread(run);
    }

    void SubmissionQueue::stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            running = false;
        }

        wake.notify_all();

        if (worker.joinable()) {
            worker.join();
        }

        while (saving > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::future<ApiResult<Submission>> SubmissionQueue::submit(const Chart& chart, Replay replay, std::shared_future<ApiResult<PlayTicket>> ticket, std::filesystem::path replay_directory) {
        bool sending = ticket.valid();
        std::string username = Account::signed_in() ? Account::username() : std::string();
        std::string name = std::format("{} - {} [{}]", chart.info.artist, chart.info.title, chart.info.difficulty_name);
        std::shared_ptr<Chart> snapshot = sending ? std::shared_ptr<Chart>(Chart::restore(chart.file_path, chart.unpitched_chart_info(), chart.unpitched_hit_objects(), chart.unpitched_timing(), chart.gameplay_hash(), chart.difficulty_key(), 1.0f)) : nullptr;
        saving++;

        std::future<ApiResult<Submission>> result = run_detached([replay = std::move(replay), replay_directory = std::move(replay_directory), snapshot, sending, ticket, username, name]() mutable {
            std::string error;

            if (!save_replay(replay, replay_directory, error)) {
                std::cerr << std::format("Replay not saved: {}", error) << std::endl;
            }

            saving--;

            if (!sending || snapshot == nullptr) {
                return ApiResult<Submission>();
            }

            nlohmann::json body = {{"chart_hash", snapshot->gameplay_hash()}, {"replay", replay}};
            nlohmann::json chart_data = snapshot->serialize();
            std::string stem = std::format("{}-{}", replay.played_at, sha256_hex(body.dump()).substr(0, 12));
            std::erase_if(stem, [](char c) { return c == ':' || c == '-'; });

            if (ticket.valid() && ticket.get().ok() && !ticket.get().value->ticket.empty()) {
                body["ticket"] = ticket.get().value->ticket;
            }

            ApiResult<Submission> result = ServerApi::send_submission(body, chart_data);

            if (result.ok() || !ServerApi::is_retryable(result.response)) {
                return result;
            }

            PendingSubmission pending;
            pending.path = SubmissionQueue::directory() / utf8_path(stem + ".json");
            pending.body = std::move(body);
            pending.chart = std::move(chart_data);
            pending.username = username;
            pending.name = name;
            pending.attempts = 1;
            pending.created_at = unix_seconds();
            pending.next_attempt_at = pending.created_at + (int64_t)std::ceil(SubmissionQueue::retry_delay_seconds(1));

            if (write(pending)) {
                wake.notify_all();
            }

            return result;
        });

        return sending ? std::move(result) : std::future<ApiResult<Submission>>();
    }

    std::vector<SubmissionNotice> SubmissionQueue::take_notices() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(notices, {});
    }
}
