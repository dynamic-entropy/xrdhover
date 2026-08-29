#ifndef XRDHOVER_AUTH_CHECK_HH
#define XRDHOVER_AUTH_CHECK_HH

#include "xrdhover/workload_spec.hh"

#include <string>
#include <vector>

namespace xrdhover {

// Extra lifetime required beyond the configured workload duration.
inline constexpr double kCredentialSafetyMarginSec = 300.0;

struct AuthCheckResult {
    bool ok = false;
    std::vector<ValidationIssue> issues;
    std::string proxy_path;   // path only — never credential contents
    double remaining_s = 0.0;
    bool loose_permissions = false;  // group/other writable
};

// Resolve X509_USER_PROXY or /tmp/x509up_u<uid>.
std::string DefaultX509ProxyPath();

// Check proxy presence, permissions, and notAfter vs
// required_duration_s + kCredentialSafetyMarginSec.
// Never returns or logs credential contents.
// proxy_path_override empty → DefaultX509ProxyPath().
AuthCheckResult CheckX509Credentials(double required_duration_s,
                                     const std::string& proxy_path_override = {});

}  // namespace xrdhover

#endif  // XRDHOVER_AUTH_CHECK_HH
