#ifndef XRDHOVER_PROM_ENCODE_HH
#define XRDHOVER_PROM_ENCODE_HH

#include "xrdhover/metrics.hh"

#include <string>

namespace xrdhover {

// Encode a MetricsSnapshot as Prometheus text exposition (0.0.4).
// Common labels: run_id, job_id, target, endpoint (from the snapshot).
// Histograms use cumulative _bucket{le="..."} + _sum + _count.
// Every metric name is prefixed with xrdhover_ (Prometheus application namespace).
std::string EncodePrometheusText(const MetricsSnapshot& snap);

}  // namespace xrdhover

#endif  // XRDHOVER_PROM_ENCODE_HH
