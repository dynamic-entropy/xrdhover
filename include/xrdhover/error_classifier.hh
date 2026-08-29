#ifndef XRDHOVER_ERROR_CLASSIFIER_HH
#define XRDHOVER_ERROR_CLASSIFIER_HH

#include <cstddef>
#include <string>

namespace xrdhover {

enum class ErrorClass {
    None,
    Auth,
    Timeout,
    Connection,
    ServerError,
    NotFound,
    ClientError,
    RedirectLoop,
    Trust,  // CA / certificate verification failures
    Unknown,
};

inline constexpr size_t kErrorClassCount = static_cast<size_t>(ErrorClass::Unknown) + 1;

const char* ErrorClassName(ErrorClass c);

// Classify from XRootD status/err codes (and optional message for heuristics).
ErrorClass ClassifyXRootDError(int status_code, int err_code, const std::string& message = {});

}  // namespace xrdhover

#endif  // XRDHOVER_ERROR_CLASSIFIER_HH
