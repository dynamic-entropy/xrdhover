#ifndef XRDHOVER_PUSH_GROUP_HH
#define XRDHOVER_PUSH_GROUP_HH

#include <string>

namespace xrdhover {

// Pushgateway grouping contract. Read this before changing Grafana, PromQL,
// or tckestrel job identity.
//
// PUT/DELETE URL:
//   /metrics/job/<push_job>/src_dst/<run_id>/replica/<job_id>
//
//   src_dst  = workload run_id = SOURCE__DEST
//              Grafana dimension (variable + legend). Shared by all N jobs
//              on one matrix cell.
//   replica  = sinks.job_id (hostname / "local" if unset)
//              Uniqueness only. tckestrel sets SOURCE__DEST__k when N > 1.
//              NOT a Grafana dimension — no variable, no legend, no split.
//
// Why replica exists
//   N jobs on one link share src_dst. Pushgateway last-PUT-wins per grouping
//   key. Without replica, Grafana Target rate is one job_rate (R/N), not the
//   cell total R. That is how a 10 Gbps hold can show ~5 Gb/s.
//
// Grafana DC27 rules (do not "simplify" these away):
//   Both dashboards (job pinned per file: xrdhover vs integrations/unix):
//     gauges:     last_over_time(<gauge>[5m])  (not the instant selector)
//     freshness:  and on (job, src_dst, job_id)
//                 (time() - last_over_time(xrdhover_push_time_seconds[5m])) < 300
//     Do not join on replica / Pushgateway push_time_seconds — gauges are
//     labeled job_id. replica stays on the PUT URL only (uniqueness).
//     last_over_time on the gauge keeps a live job in the sum across a
//     missed scrape; freshness last_over_time covers steps before the next
//     scrape. 300s is past chirp-stretch + skew. Instant gauges + and
//     drop one job_rate from Target/Achieved for a single Grafana step.
//   display:    sum by (source, dest, src_dst)   [plus the panel key]
//               so N replicas collapse to one SOURCE__DEST series
//   variables:  job (pinned per dashboard), source, dest, src_dst only
//   legends:    {{src_dst}} (plus class / cms_site / …)
//
// Wipe groups that pre-date replica, then wait one scrape:
//   DELETE https://xrdprom.cern.ch:2094/metrics/job/xrdhover

inline constexpr const char kPushReplicaLabel[] = "replica";

inline std::string PushReplicaValue(const std::string& job_id) {
    return job_id.empty() ? std::string("local") : job_id;
}

// Path after the Pushgateway origin. job / src_dst / replica are already
// URL-encoded when called from PushgatewaySink.
inline std::string FormatPushGroupPath(const std::string& job, const std::string& src_dst,
                                      const std::string& replica) {
    return "/metrics/job/" + job + "/src_dst/" + src_dst + "/" + kPushReplicaLabel + "/" + replica;
}

}  // namespace xrdhover

#endif  // XRDHOVER_PUSH_GROUP_HH
