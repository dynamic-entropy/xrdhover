#ifndef XRDHOVER_LOG_HH
#define XRDHOVER_LOG_HH

#include <cstdio>
#include <string>

namespace xrdhover {

// UTC timestamp with milliseconds, e.g. "2026-08-02T10:15:30.123Z".
std::string UtcStamp();

// Print "<stamp> <fmt...>\n" to stream (fmt should omit the trailing newline).
void LogTo(std::FILE* stream, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace xrdhover

#define XRDHOVER_LOG_ERR(...) ::xrdhover::LogTo(stderr, __VA_ARGS__)
#define XRDHOVER_LOG_OUT(...) ::xrdhover::LogTo(stdout, __VA_ARGS__)

#endif  // XRDHOVER_LOG_HH
