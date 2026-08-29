#include "xrdhover/run_command.hh"

#include <cstdio>

namespace xrdhover {

// Linked only into xrdhover-tests so workload_run_command.cc resolves
// RunRunCommand without pulling the full engine. Production tests always set
// WorkloadRunOptions::run_hook and should never reach this stub.
int RunRunCommand(const RunConfig&) {
    std::fprintf(stderr, "error: stub RunRunCommand called (set run_hook in tests)\n");
    return 99;
}

}  // namespace xrdhover
