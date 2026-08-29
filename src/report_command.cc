#include "xrdhover/report_command.hh"

#include "xrdhover/units.hh"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace xrdhover {
namespace {

uint64_t JsonU64(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return 0;
    if (j[key].is_number_unsigned()) return j[key].get<uint64_t>();
    if (j[key].is_number_integer()) {
        const auto v = j[key].get<int64_t>();
        return v < 0 ? 0 : static_cast<uint64_t>(v);
    }
    if (j[key].is_number_float()) return static_cast<uint64_t>(j[key].get<double>());
    return 0;
}

double JsonDouble(const json& j, const char* key) {
    if (!j.contains(key) || j[key].is_null()) return 0.0;
    if (j[key].is_number()) return j[key].get<double>();
    return 0.0;
}

std::string JsonString(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_string()) return {};
    return j[key].get<std::string>();
}

void MergeCountMap(std::map<std::string, uint64_t>& dst, const json& obj) {
    if (!obj.is_object()) return;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.value().is_number()) {
            dst[it.key()] += static_cast<uint64_t>(it.value().get<double>());
        }
    }
}

void MergeBucket(AttributionBucket& dst, const json& entry) {
    dst.bytes_read += JsonU64(entry, "bytes_read");
    dst.sessions_ok += JsonU64(entry, "sessions_ok");
    dst.sessions_fail += JsonU64(entry, "sessions_fail");
    if (entry.contains("errors")) MergeCountMap(dst.errors, entry["errors"]);
    if (dst.cms_site.empty()) dst.cms_site = JsonString(entry, "cms_site");
}

void PrintBucketTable(const char* title, const std::map<std::string, AttributionBucket>& m,
                      bool show_cms_site) {
    if (m.empty()) return;
    std::printf("%s:\n", title);
    for (const auto& kv : m) {
        const AttributionBucket& b = kv.second;
        if (show_cms_site && !b.cms_site.empty()) {
            std::printf("  %-40s site=%-24s bytes=%-12s ok=%" PRIu64 " fail=%" PRIu64 "\n",
                        kv.first.c_str(), b.cms_site.c_str(), FormatBytes(b.bytes_read).c_str(),
                        b.sessions_ok, b.sessions_fail);
        } else {
            std::printf("  %-40s bytes=%-12s ok=%" PRIu64 " fail=%" PRIu64 "\n", kv.first.c_str(),
                        FormatBytes(b.bytes_read).c_str(), b.sessions_ok, b.sessions_fail);
        }
    }
}

json BucketToJson(const AttributionBucket& b, bool include_cms_site) {
    json entry = {{"bytes_read", b.bytes_read},
                  {"sessions_ok", b.sessions_ok},
                  {"sessions_fail", b.sessions_fail},
                  {"errors", b.errors}};
    if (include_cms_site && !b.cms_site.empty()) entry["cms_site"] = b.cms_site;
    return entry;
}

json SummaryToJson(const ReportSummary& s) {
    const double achieved =
        s.elapsed_s > 0.0 ? static_cast<double>(s.bytes_read) / s.elapsed_s : 0.0;
    json by_ds = json::object();
    for (const auto& kv : s.by_data_server) by_ds[kv.first] = BucketToJson(kv.second, true);
    json by_site = json::object();
    for (const auto& kv : s.by_cms_site) by_site[kv.first] = BucketToJson(kv.second, false);

    return {{"run_id", s.run_id},
            {"sources", s.sources},
            {"job_ids", s.job_ids},
            {"endpoint", s.endpoint},
            {"target", s.target},
            {"elapsed_s", s.elapsed_s},
            {"bytes_read", s.bytes_read},
            {"sessions_ok", s.sessions_ok},
            {"sessions_fail", s.sessions_fail},
            {"ops", s.ops},
            {"achieved_bytes_per_s", achieved},
            {"achieved_bits_per_s", achieved * 8.0},
            {"errors", s.errors},
            {"soft_faults", s.soft_faults},
            {"by_data_server", std::move(by_ds)},
            {"by_cms_site", std::move(by_site)}};
}

void PrintHuman(const ReportSummary& s) {
    const double achieved =
        s.elapsed_s > 0.0 ? static_cast<double>(s.bytes_read) / s.elapsed_s : 0.0;

    std::printf("=== report ===\n");
    std::printf("run_id:         %s\n", s.run_id.c_str());
    if (s.job_ids.size() == 1) {
        std::printf("job_id:         %s\n", s.job_ids.front().c_str());
    } else if (!s.job_ids.empty()) {
        std::printf("job_ids:        %zu\n", s.job_ids.size());
        for (const auto& id : s.job_ids) std::printf("  %s\n", id.c_str());
    }
    if (!s.endpoint.empty()) std::printf("endpoint:       %s\n", s.endpoint.c_str());
    if (!s.target.empty()) std::printf("target:         %s\n", s.target.c_str());
    if (!s.sources.empty()) std::printf("results:        %s\n", s.sources.front().c_str());
    std::printf("elapsed:        %s\n", FormatDuration(s.elapsed_s).c_str());
    std::printf("sessions:       %" PRIu64 " ok / %" PRIu64 " fail\n", s.sessions_ok,
                s.sessions_fail);
    std::printf("bytes:          %s (%" PRIu64 ")\n", FormatBytes(s.bytes_read).c_str(),
                s.bytes_read);
    std::printf("ops:            %" PRIu64 "\n", s.ops);
    std::printf("achieved:       %s\n", FormatRate(static_cast<uint64_t>(achieved)).c_str());
    if (!s.errors.empty()) {
        std::printf("errors:\n");
        for (const auto& e : s.errors) {
            std::printf("  %-14s %" PRIu64 "\n", e.first.c_str(), e.second);
        }
    }
    if (!s.soft_faults.empty()) {
        std::printf("soft_faults:\n");
        for (const auto& e : s.soft_faults) {
            std::printf("  %-14s %" PRIu64 "\n", e.first.c_str(), e.second);
        }
    }
    PrintBucketTable("by_cms_site", s.by_cms_site, false);
    PrintBucketTable("by_data_server", s.by_data_server, true);
}

std::string RequireRunId(const ReportOptions& opts, const char* why) {
    if (!opts.run_id.empty()) return opts.run_id;
    throw std::runtime_error(std::string(why) + " (pass --run-id)");
}

}  // namespace

std::vector<std::string> DiscoverResultFiles(const ReportOptions& opts) {
    const bool have_path = !opts.path.empty();
    const bool have_results = !opts.results_dir.empty();

    if (!have_path && !have_results) {
        throw std::runtime_error("pass a PATH (run dir or result.json) or --results-dir");
    }
    if (have_path && have_results) {
        throw std::runtime_error("pass PATH or --results-dir, not both");
    }

    std::error_code ec;

    if (have_path) {
        const fs::path p = fs::path(opts.path);
        if (fs::is_regular_file(p, ec)) {
            if (p.filename() != "result.json") {
                throw std::runtime_error("file PATH must be result.json");
            }
            return {p.string()};
        }
        if (!fs::is_directory(p, ec)) {
            throw std::runtime_error("PATH not found: " + opts.path);
        }

        const fs::path direct = p / "result.json";
        if (fs::is_regular_file(direct, ec)) return {direct.string()};

        const std::string run_id =
            RequireRunId(opts, "PATH has no result.json; treat as results root");
        const fs::path single = p / run_id / "result.json";
        if (fs::is_regular_file(single, ec)) return {single.string()};
        throw std::runtime_error("no result.json for run_id=" + run_id + " under " + p.string());
    }

    const std::string run_id = RequireRunId(opts, "--results-dir needs --run-id");
    const fs::path root = fs::path(opts.results_dir);
    if (!fs::is_directory(root, ec)) {
        throw std::runtime_error("results dir not found: " + opts.results_dir);
    }
    const fs::path single = root / run_id / "result.json";
    if (fs::is_regular_file(single, ec)) return {single.string()};
    throw std::runtime_error("no result.json for run_id=" + run_id + " under " + opts.results_dir);
}

ReportSummary AggregateResultFiles(const std::vector<std::string>& paths) {
    if (paths.empty()) throw std::runtime_error("no result files to aggregate");

    ReportSummary out;
    out.sources = paths;

    std::set<std::string> endpoints;
    std::set<std::string> targets;
    std::set<std::string> run_ids;
    std::set<std::string> job_ids;

    for (const auto& path : paths) {
        std::ifstream in(path);
        if (!in) throw std::runtime_error("cannot open " + path);
        json j;
        try {
            in >> j;
        } catch (const json::exception& e) {
            throw std::runtime_error("invalid JSON in " + path + ": " + e.what());
        }
        if (!j.is_object()) throw std::runtime_error("result.json root must be object: " + path);

        const std::string rid = JsonString(j, "run_id");
        if (!rid.empty()) run_ids.insert(rid);
        const std::string jid = JsonString(j, "job_id");
        if (!jid.empty()) job_ids.insert(jid);
        const std::string ep = JsonString(j, "endpoint");
        if (!ep.empty()) endpoints.insert(ep);
        const std::string tg = JsonString(j, "target");
        if (!tg.empty()) targets.insert(tg);

        out.bytes_read += JsonU64(j, "bytes_read");
        out.sessions_ok += JsonU64(j, "sessions_ok");
        out.sessions_fail += JsonU64(j, "sessions_fail");
        out.ops += JsonU64(j, "ops");
        out.elapsed_s = std::max(out.elapsed_s, JsonDouble(j, "elapsed_s"));

        if (j.contains("errors")) MergeCountMap(out.errors, j["errors"]);
        if (j.contains("soft_faults")) MergeCountMap(out.soft_faults, j["soft_faults"]);

        if (j.contains("by_data_server") && j["by_data_server"].is_object()) {
            for (auto it = j["by_data_server"].begin(); it != j["by_data_server"].end(); ++it) {
                MergeBucket(out.by_data_server[it.key()], it.value());
            }
        }
        if (j.contains("by_cms_site") && j["by_cms_site"].is_object()) {
            for (auto it = j["by_cms_site"].begin(); it != j["by_cms_site"].end(); ++it) {
                MergeBucket(out.by_cms_site[it.key()], it.value());
            }
        }
    }

    if (run_ids.size() == 1) {
        out.run_id = *run_ids.begin();
    } else if (run_ids.size() > 1) {
        throw std::runtime_error("result files disagree on run_id");
    }
    if (endpoints.size() == 1) out.endpoint = *endpoints.begin();
    if (targets.size() == 1) out.target = *targets.begin();
    out.job_ids.assign(job_ids.begin(), job_ids.end());
    return out;
}

int RunReportCommand(const ReportOptions& opts) {
    try {
        const auto files = DiscoverResultFiles(opts);
        const ReportSummary summary = AggregateResultFiles(files);
        if (opts.json) {
            std::printf("%s\n", SummaryToJson(summary).dump(2).c_str());
        } else {
            PrintHuman(summary);
        }
        if (summary.sessions_fail > 0 && summary.sessions_ok == 0) return 1;
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 2;
    }
}

}  // namespace xrdhover
