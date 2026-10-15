#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace nascent {
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

    template <typename F>
    void parallel_for(size_t count, F work) {
        size_t thread_count = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), std::max<size_t>(count, 1));
        std::atomic<size_t> next = 0;
        std::vector<std::thread> threads;

        for (size_t t = 0; t < thread_count; t++) {
            threads.emplace_back([&] {
                for (size_t index = next++; index < count; index = next++) {
                    work(index);
                }
            });
        }

        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    template <typename Future>
    bool is_ready(const Future& future) {
        return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
}
