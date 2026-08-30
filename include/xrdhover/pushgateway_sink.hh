#ifndef XRDHOVER_PUSHGATEWAY_SINK_HH
#define XRDHOVER_PUSHGATEWAY_SINK_HH

#include "xrdhover/metrics.hh"
#include "xrdhover/push_group.hh"

#include <curl/curl.h>

#include <string>

namespace xrdhover {

// Pushes Prometheus text to a Pushgateway.
// Grouping: job + src_dst + replica. See push_group.hh — replica is
// uniqueness only and must not become a Grafana dimension.
// Deletes the grouping key on Finish() (clean exit).
//
// Reuses one libcurl easy handle across Push/Finish so TCP/TLS connections
// and DNS cache survive between snapshot intervals.
class PushgatewaySink {
public:
    // base_url e.g. https://xrdprom.cern.ch:2094
    PushgatewaySink(std::string base_url, std::string push_job = "xrdhover");
    ~PushgatewaySink();

    PushgatewaySink(const PushgatewaySink&) = delete;
    PushgatewaySink& operator=(const PushgatewaySink&) = delete;

    // PUT current snapshot. Returns false on HTTP/transport failure (logged).
    bool Push(const MetricsSnapshot& snap);

    // DELETE the grouping key. Safe to call multiple times.
    void Finish(const std::string& src_dst, const std::string& replica);

    const std::string& base_url() const { return base_url_; }

private:
    std::string GroupUrl(const std::string& src_dst, const std::string& replica);
    bool HttpRequest(const char* method, const std::string& url, const std::string& body,
                     long* http_code_out);
    std::string UrlEncode(const std::string& s);

    std::string base_url_;
    std::string push_job_;
    std::string encoded_job_;
    std::string last_src_dst_;
    std::string last_replica_;
    std::string cached_group_url_;
    bool finished_ = false;

    CURL* curl_ = nullptr;
    struct curl_slist* put_headers_ = nullptr;
};

}  // namespace xrdhover

#endif  // XRDHOVER_PUSHGATEWAY_SINK_HH
