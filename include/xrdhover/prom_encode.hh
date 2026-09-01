#ifndef XRDHOVER_PROM_ENCODE_HH
#define XRDHOVER_PROM_ENCODE_HH

#include "xrdhover/metrics.hh"

#include <string>

namespace xrdhover {

// Encode a MetricsSnapshot as Prometheus text exposition (0.0.4).
//
// The live view is a *link* (src_dst = SOURCE__DEST):
//   Layer 1 — link: bytes, sessions, achieved/target rate, inflight,
//             max_inflight, errors, histograms.
//   Layer 2 — cms_site attribution: site_bytes, site_sessions,
//             site_achieved_rate. Unmapped disks use cms_site="unmapped".
// Disk hosts stay in result.json.
//
// Common labels: src_dst (workload run_id / SOURCE__DEST), job_id, target,
// endpoint, source, dest. Pushgateway grouping is src_dst + replica (job_id).
// replica is uniqueness only — see push_group.hh. Not a Grafana dimension.
// Histograms use cumulative _bucket{le="..."} + _sum + _count.
// Every metric name is prefixed with xrdhover_ (Prometheus application namespace).
std::string EncodePrometheusText(const MetricsSnapshot& snap);

}  // namespace xrdhover

#endif  // XRDHOVER_PROM_ENCODE_HH
