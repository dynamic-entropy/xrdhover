#ifndef XRDHOVER_PROM_ENCODE_HH
#define XRDHOVER_PROM_ENCODE_HH

#include "xrdhover/metrics.hh"

#include <string>

namespace xrdhover {

// Encode a MetricsSnapshot as Prometheus text exposition (0.0.4).
// Common labels: src_dst (workload run_id / SOURCE__DEST), job_id, target,
// endpoint, source, dest. Pushgateway grouping key is src_dst, not instance.
// Histograms use cumulative _bucket{le="..."} + _sum + _count.
// Every metric name is prefixed with xrdhover_ (Prometheus application namespace).
std::string EncodePrometheusText(const MetricsSnapshot& snap);

}  // namespace xrdhover

#endif  // XRDHOVER_PROM_ENCODE_HH
