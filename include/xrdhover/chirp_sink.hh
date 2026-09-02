#ifndef XRDHOVER_CHIRP_SINK_HH
#define XRDHOVER_CHIRP_SINK_HH

#include "xrdhover/metrics.hh"

#include <string>

namespace xrdhover {

// Publishes metrics via condor_chirp:
//   - set_job_attr: immediate ClassAd updates for control-loop scalars
//     (achieved rate, bytes, sessions, errors, inflight, wall/cpu time).
//   - put: full Prometheus text exposition file to a remote directory on
//     the access point (for node_exporter textfile collector).
//
// Degrades gracefully: if condor_chirp is not found (local dev runs,
// non-Condor execution), all calls become no-ops.
class ChirpSink {
public:
    // chirp_binary: explicit path to condor_chirp (empty = auto-discover).
    // prom_remote_dir: AP directory for .prom files (empty = prom disabled).
    // classads: enable set_job_attr for control-loop scalars.
    // run_id / job_id: used to construct the .prom filename.
    ChirpSink(std::string chirp_binary, std::string prom_remote_dir,
              bool classads, std::string run_id, std::string job_id);

    ChirpSink(const ChirpSink&) = delete;
    ChirpSink& operator=(const ChirpSink&) = delete;

    // Called every snapshot_interval from the timer thread.
    void Push(const MetricsSnapshot& snap);

    // Called on clean exit: push final snapshot with zeroed rate/inflight,
    // then remove the remote .prom file.
    void Finish(const MetricsSnapshot& final_snap);

    bool Available() const { return !chirp_binary_.empty(); }

private:
    bool RunChirp(const char* const argv[], int argc) const;
    bool SetAttr(const char* name, const std::string& value) const;
    bool SetAttr(const char* name, double value) const;
    bool SetAttr(const char* name, uint64_t value) const;
    bool SetAttr(const char* name, uint32_t value) const;
    bool PutFile(const std::string& remote_path, const std::string& body) const;
    bool RemoveFile(const std::string& remote_path) const;

    void PushClassAds(const MetricsSnapshot& snap) const;
    void PushPromFile(const MetricsSnapshot& snap) const;

    static std::string DiscoverChirpBinary();

    std::string chirp_binary_;
    std::string prom_remote_path_;   // full remote path: dir/xrdhover_{run_id}_{job_id}.prom
    bool classads_ = true;
};

}  // namespace xrdhover

#endif  // XRDHOVER_CHIRP_SINK_HH
