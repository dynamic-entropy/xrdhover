#ifndef XRDHOVER_RUN_COMMAND_HH
#define XRDHOVER_RUN_COMMAND_HH

#include "xrdhover/run_config.hh"

namespace xrdhover {

// Execute a CLI-driven run (or --dry-run). Returns process exit code.
int RunRunCommand(const RunConfig& cfg);

}  // namespace xrdhover

#endif  // XRDHOVER_RUN_COMMAND_HH
