#include "xrdhover/token_bucket.hh"

#include <algorithm>
#include <thread>

namespace xrdhover {

TokenBucket::TokenBucket(uint64_t rate_bytes_per_s, uint64_t capacity_bytes)
    : rate_bytes_per_s_(rate_bytes_per_s),
      capacity_(capacity_bytes > 0 ? capacity_bytes : (rate_bytes_per_s > 0 ? rate_bytes_per_s : 1)),
      tokens_(static_cast<double>(capacity_)),
      last_(std::chrono::steady_clock::now()) {}

void TokenBucket::RefillUnlocked(std::chrono::steady_clock::time_point now) {
    if (rate_bytes_per_s_ == 0) return;
    const double dt = std::chrono::duration<double>(now - last_).count();
    if (dt <= 0) return;
    tokens_ = std::min(static_cast<double>(capacity_), tokens_ + dt * static_cast<double>(rate_bytes_per_s_));
    last_ = now;
}

bool TokenBucket::TryAcquire(uint64_t n) {
    if (rate_bytes_per_s_ == 0 || n == 0) return true;
    if (n > capacity_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    RefillUnlocked(std::chrono::steady_clock::now());
    if (tokens_ < static_cast<double>(n)) return false;
    tokens_ -= static_cast<double>(n);
    return true;
}

bool TokenBucket::AcquireUntil(uint64_t n, std::chrono::steady_clock::time_point deadline) {
    if (rate_bytes_per_s_ == 0 || n == 0) return true;
    // Charge larger than capacity can never succeed.
    if (n > capacity_) return false;

    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            RefillUnlocked(std::chrono::steady_clock::now());
            if (tokens_ >= static_cast<double>(n)) {
                tokens_ -= static_cast<double>(n);
                return true;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;

        double deficit = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            RefillUnlocked(now);
            deficit = static_cast<double>(n) - tokens_;
        }
        if (deficit <= 0) continue;

        auto sleep_for = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(deficit / static_cast<double>(rate_bytes_per_s_)));
        sleep_for = std::min(sleep_for, std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - now));
        sleep_for = std::min(sleep_for, std::chrono::nanoseconds(20'000'000));
        if (sleep_for.count() > 0) std::this_thread::sleep_for(sleep_for);
    }
}

double TokenBucket::SecondsUntil(uint64_t n) {
    if (rate_bytes_per_s_ == 0 || n == 0) return 0.0;
    if (n > capacity_) return 1e12;
    std::lock_guard<std::mutex> lock(mu_);
    RefillUnlocked(std::chrono::steady_clock::now());
    if (tokens_ >= static_cast<double>(n)) return 0.0;
    return (static_cast<double>(n) - tokens_) / static_cast<double>(rate_bytes_per_s_);
}

void TokenBucket::Refund(uint64_t n) {
    if (rate_bytes_per_s_ == 0 || n == 0) return;
    std::lock_guard<std::mutex> lock(mu_);
    RefillUnlocked(std::chrono::steady_clock::now());
    tokens_ = std::min(static_cast<double>(capacity_), tokens_ + static_cast<double>(n));
}

}  // namespace xrdhover
