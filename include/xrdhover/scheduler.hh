#ifndef XRDHOVER_SCHEDULER_HH
#define XRDHOVER_SCHEDULER_HH

#include "xrdhover/file_session.hh"
#include "xrdhover/run_config.hh"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace xrdhover {

struct WorkItem {
    FileSessionOptions session;
    uint64_t op_bytes = 0;  // tokens to acquire before this session's first Read
};

// Seeded work generator: picks file + session options from RunConfig.
class Scheduler {
   public:
    explicit Scheduler(const RunConfig& cfg);

    WorkItem Next();

   private:
    const RunConfig& cfg_;
    std::mt19937_64 rng_;
    uint64_t seq_ = 0;
};

}  // namespace xrdhover

#endif  // XRDHOVER_SCHEDULER_HH
