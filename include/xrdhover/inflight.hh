#ifndef XRDHOVER_INFLIGHT_HH
#define XRDHOVER_INFLIGHT_HH

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace xrdhover {

// Caps concurrent FileSessions. Not an OS thread pool.
class InFlightSemaphore {
   public:
    explicit InFlightSemaphore(uint32_t max_inflight);

    // Block until a slot is free or deadline passes. Returns false on timeout.
    bool AcquireUntil(std::chrono::steady_clock::time_point deadline);

    // Non-blocking. Returns false if already at max.
    bool TryAcquire();

    void Release();

    uint32_t current() const;
    uint32_t max() const { return max_; }

   private:
    const uint32_t max_;
    uint32_t current_ = 0;
    mutable std::mutex mu_;
    std::condition_variable cv_;
};

}  // namespace xrdhover

#endif  // XRDHOVER_INFLIGHT_HH
