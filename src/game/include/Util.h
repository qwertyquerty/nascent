#include <atomic>
#include <mutex>
#include <condition_variable>

#pragma once

namespace nascent {
    float smoothstep(double begin, double end, double t);
    
    inline int positive_modulo(int i, int n) {
        return (i % n + n) % n;
    }

    class LatestWinsMutex {
    public:
        // Latest-wins lock: invalidates older waiters
        bool lock_latest() {
            std::unique_lock<std::mutex> lk(mutex_);

            // If a greedy holder exists, wait normally (no invalidation allowed)
            while (locked_ && greedy_) {
                cv_.wait(lk);
            }

            // Invalidate prior waiters
            size_t my_generation = ++generation_;

            while (true) {
                if (my_generation != generation_) {
                    return false; // invalidated by newer waiter
                }

                if (!locked_) {
                    locked_ = true;
                    greedy_ = false;
                    return true;
                }

                cv_.wait(lk);
            }
        }

        // Try to acquire greedy lock without blocking
        bool try_lock_greedy() {
            std::lock_guard<std::mutex> lk(mutex_);

            if (locked_) {
                return false;
            }

            locked_ = true;
            greedy_ = true;
            return true;
        }

        void unlock() {
            std::lock_guard<std::mutex> lk(mutex_);
            locked_ = false;
            greedy_ = false;
            cv_.notify_all();
        }

    private:
        std::mutex mutex_;
        std::condition_variable cv_;

        size_t generation_ = 0;  // protected by mutex_
        bool locked_ = false;
        bool greedy_ = false;
    };
}
