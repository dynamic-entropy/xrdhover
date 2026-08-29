#ifndef XRDHOVER_XRDCL_ENV_HH
#define XRDHOVER_XRDCL_ENV_HH

namespace xrdhover {

// Apply XrdCl DefaultEnv timeout knobs used by run and probe.
// Logs the applied values to stderr when log=true.
void ApplyXrdClTimeouts(int connection_window_s, int connection_retry, int request_timeout_s,
                        double session_timeout_s, bool log = true);

}  // namespace xrdhover

#endif  // XRDHOVER_XRDCL_ENV_HH
