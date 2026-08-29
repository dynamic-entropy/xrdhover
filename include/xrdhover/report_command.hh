#ifndef XRDHOVER_REPORT_COMMAND_HH
#define XRDHOVER_REPORT_COMMAND_HH

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace xrdhover {

struct ReportOptions {
    std::string path;         // run dir or result.json
    std::string results_dir;  // --results-dir (with --run-id)
    std::string run_id;       // --run-id
    bool json = false;        // --json
};

struct AttributionBucket {
    uint64_t bytes_read = 0;
    uint64_t sessions_ok = 0;
    uint64_t sessions_fail = 0;
    std::map<std::string, uint64_t> errors;
    std::string cms_site;  // set on by_data_server entries when present
};

struct ReportSummary {
    std::string run_id;
    std::vector<std::string> job_ids;
    std::vector<std::string> sources;
    std::string endpoint;
    std::string target;
    double elapsed_s = 0.0;
    uint64_t bytes_read = 0;
    uint64_t sessions_ok = 0;
    uint64_t sessions_fail = 0;
    uint64_t ops = 0;
    std::map<std::string, uint64_t> errors;
    std::map<std::string, uint64_t> soft_faults;
    std::map<std::string, AttributionBucket> by_data_server;
    std::map<std::string, AttributionBucket> by_cms_site;
};

// Resolve a single result.json. Throws std::runtime_error on missing input.
std::vector<std::string> DiscoverResultFiles(const ReportOptions& opts);

// Load and merge one or more FileSink result.json files.
ReportSummary AggregateResultFiles(const std::vector<std::string>& paths);

// Discover → aggregate → print. Exit 0 ok, 1 all sessions failed, 2 usage/IO.
int RunReportCommand(const ReportOptions& opts);

}  // namespace xrdhover

#endif  // XRDHOVER_REPORT_COMMAND_HH
