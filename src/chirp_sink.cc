#include "xrdhover/chirp_sink.hh"

#include "xrdhover/log.hh"
#include "xrdhover/prom_encode.hh"

#include <array>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace xrdhover {
namespace {

namespace fs = std::filesystem;

constexpr int kChirpTimeoutS = 10;

// Build an environ copy with PATH/PYTHONPATH/PYTHONHOME restored from
// XRDHOVER_ORIG_* so the glidein's Python is used for condor_chirp.
// Must be done BEFORE fork() — after fork in a multithreaded process
// only async-signal-safe functions are allowed (no malloc/setenv).
struct ChirpEnv {
    std::vector<std::string> storage;
    std::vector<const char*> ptrs;

    ChirpEnv() {
        const char* orig_path = std::getenv("XRDHOVER_ORIG_PATH");
        const char* orig_pypath = std::getenv("XRDHOVER_ORIG_PYTHONPATH");
        const char* orig_pyhome = std::getenv("XRDHOVER_ORIG_PYTHONHOME");
        bool has_path = false;
        for (char** e = environ; e && *e; ++e) {
            std::string_view entry(*e);
            if (orig_path && *orig_path && entry.substr(0, 5) == "PATH=") {
                storage.push_back(std::string("PATH=") + orig_path);
                ptrs.push_back(storage.back().c_str());
                has_path = true;
            } else if (entry.substr(0, 11) == "PYTHONPATH=") {
                if (orig_pypath && *orig_pypath) {
                    storage.push_back(std::string("PYTHONPATH=") + orig_pypath);
                    ptrs.push_back(storage.back().c_str());
                }
            } else if (entry.substr(0, 11) == "PYTHONHOME=") {
                if (orig_pyhome && *orig_pyhome) {
                    storage.push_back(std::string("PYTHONHOME=") + orig_pyhome);
                    ptrs.push_back(storage.back().c_str());
                }
            } else {
                ptrs.push_back(*e);
            }
        }
        if (orig_path && *orig_path && !has_path) {
            storage.push_back(std::string("PATH=") + orig_path);
            ptrs.push_back(storage.back().c_str());
        }
        ptrs.push_back(nullptr);
    }
};

// Fork+exec condor_chirp with the given arguments.  Returns true on exit 0.
// Kills the child after kChirpTimeoutS seconds if it hasn't exited.
// All heap allocation happens before fork(); the child only calls
// async-signal-safe functions (open/dup2/close/execve/_exit).
bool ForkExecChirp(const std::string& binary, const char* const argv[], int argc) {
    // Build argv and environ before fork (heap allocation is safe here).
    std::vector<const char*> av;
    av.reserve(argc + 2);
    av.push_back(binary.c_str());
    for (int i = 0; i < argc; ++i) av.push_back(argv[i]);
    av.push_back(nullptr);

    static thread_local ChirpEnv chirp_env;

    const pid_t pid = fork();
    if (pid < 0) {
        XRDHOVER_LOG_ERR("chirp: fork failed: %s", std::strerror(errno));
        return false;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        execve(binary.c_str(), const_cast<char* const*>(av.data()),
               const_cast<char* const*>(chirp_env.ptrs.data()));
        _exit(127);
    }

    // Poll with timeout instead of blocking forever.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(kChirpTimeoutS);
    for (;;) {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc > 0) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
            return false;
        }
        if (rc < 0 && errno != EINTR) {
            XRDHOVER_LOG_ERR("chirp: waitpid failed: %s", std::strerror(errno));
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            XRDHOVER_LOG_ERR("chirp: child pid %d timed out after %ds, killing",
                             static_cast<int>(pid), kChirpTimeoutS);
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            return false;
        }
        usleep(50000);  // 50ms
    }
}

std::string TrimTrailingSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

}  // namespace

std::string ChirpSink::DiscoverChirpBinary() {
    // 1. $_CONDOR_CHIRP_CONFIG points into the starter's runtime dir.
    //    The condor_chirp binary is typically a sibling.
    if (const char* cfg = std::getenv("_CONDOR_CHIRP_CONFIG"); cfg && *cfg) {
        fs::path dir = fs::path(cfg).parent_path();
        for (const char* name : {"condor_chirp", "../bin/condor_chirp"}) {
            fs::path cand = dir / name;
            std::error_code ec;
            if (fs::exists(cand, ec) && !ec) return fs::canonical(cand, ec).string();
        }
    }
    // 2. Fallback: search PATH.
    if (const char* path_env = std::getenv("PATH"); path_env && *path_env) {
        std::string paths(path_env);
        size_t pos = 0;
        while (pos < paths.size()) {
            size_t sep = paths.find(':', pos);
            if (sep == std::string::npos) sep = paths.size();
            std::string dir = paths.substr(pos, sep - pos);
            pos = sep + 1;
            if (dir.empty()) continue;
            fs::path cand = fs::path(dir) / "condor_chirp";
            std::error_code ec;
            if (fs::exists(cand, ec) && !ec) return cand.string();
        }
    }
    return {};
}

ChirpSink::ChirpSink(std::string chirp_binary, std::string prom_remote_dir,
                       bool classads, std::string run_id, std::string job_id)
    : classads_(classads) {
    chirp_binary_ = chirp_binary.empty() ? DiscoverChirpBinary() : std::move(chirp_binary);
    if (chirp_binary_.empty()) return;

    if (!prom_remote_dir.empty()) {
        prom_remote_dir = TrimTrailingSlash(std::move(prom_remote_dir));
        prom_remote_path_ = prom_remote_dir + "/xrdhover_" + run_id + "_" + job_id + ".prom";
    }
}

bool ChirpSink::RunChirp(const char* const argv[], int argc) const {
    if (chirp_binary_.empty()) return false;
    return ForkExecChirp(chirp_binary_, argv, argc);
}

bool ChirpSink::SetAttr(const char* name, const std::string& value) const {
    const char* argv[] = {"set_job_attr", name, value.c_str()};
    return RunChirp(argv, 3);
}

bool ChirpSink::SetAttr(const char* name, double value) const {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6g", value);
    return SetAttr(name, std::string(buf));
}

bool ChirpSink::SetAttr(const char* name, uint64_t value) const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%" PRIu64, value);
    return SetAttr(name, std::string(buf));
}

bool ChirpSink::SetAttr(const char* name, uint32_t value) const {
    return SetAttr(name, static_cast<uint64_t>(value));
}

bool ChirpSink::PutFile(const std::string& remote_path, const std::string& body) const {
    // Write body to a temp file, then `condor_chirp put <local> <remote>`.
    const char* tmpdir = std::getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = "/tmp";
    std::string tmp_path = std::string(tmpdir) + "/xrdhover_chirp_XXXXXX";
    int fd = mkstemp(tmp_path.data());
    if (fd < 0) {
        XRDHOVER_LOG_ERR("chirp: mkstemp failed: %s", std::strerror(errno));
        return false;
    }
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(body.size())) {
        ssize_t n = write(fd, body.data() + written, body.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            XRDHOVER_LOG_ERR("chirp: write tmp failed: %s", std::strerror(errno));
            close(fd);
            unlink(tmp_path.c_str());
            return false;
        }
        written += n;
    }
    close(fd);

    const char* argv[] = {"put", tmp_path.c_str(), remote_path.c_str()};
    bool ok = RunChirp(argv, 3);
    unlink(tmp_path.c_str());
    if (!ok) {
        XRDHOVER_LOG_ERR("chirp: put %s failed", remote_path.c_str());
    }
    return ok;
}

bool ChirpSink::RemoveFile(const std::string& remote_path) const {
    const char* argv[] = {"remove", remote_path.c_str()};
    return RunChirp(argv, 2);
}

void ChirpSink::PushClassAds(const MetricsSnapshot& snap) const {
    SetAttr("ChirpXrdhoverAchievedRate", snap.achieved_rate_bytes);
    SetAttr("ChirpXrdhoverTargetRate", snap.target_rate_bytes);
    SetAttr("ChirpXrdhoverBytesTotal", snap.bytes_read_total);
    SetAttr("ChirpXrdhoverSessionsOk", snap.sessions_ok);
    SetAttr("ChirpXrdhoverSessionsFail", snap.sessions_fail);
    uint64_t errors_total = 0;
    for (const auto& e : snap.errors_by_class) errors_total += e.second;
    SetAttr("ChirpXrdhoverErrorsTotal", errors_total);
    uint64_t soft_faults = 0;
    for (const auto& f : snap.soft_faults_by_kind) soft_faults += f.second;
    SetAttr("ChirpXrdhoverSoftFaults", soft_faults);
    SetAttr("ChirpXrdhoverInflight", snap.inflight_reads);
    SetAttr("ChirpXrdhoverWallS", snap.wall_s);
    SetAttr("ChirpXrdhoverCpuS", snap.cpu_seconds_total);
}

void ChirpSink::PushPromFile(const MetricsSnapshot& snap) const {
    if (prom_remote_path_.empty()) return;
    const std::string body = EncodePrometheusText(snap);
    PutFile(prom_remote_path_, body);
}

void ChirpSink::Push(const MetricsSnapshot& snap) {
    if (chirp_binary_.empty()) return;
    if (classads_) PushClassAds(snap);
    PushPromFile(snap);
}

void ChirpSink::Finish(const MetricsSnapshot& final_snap) {
    if (chirp_binary_.empty()) return;

    // Push final snapshot.
    Push(final_snap);

    // Zero rate/inflight gauges so dashboards don't hold the last value.
    if (classads_) {
        SetAttr("ChirpXrdhoverAchievedRate", 0.0);
        SetAttr("ChirpXrdhoverInflight", static_cast<uint32_t>(0));
    }
    if (!prom_remote_path_.empty()) {
        MetricsSnapshot idle = final_snap;
        idle.achieved_rate_bytes = 0.0;
        idle.inflight_reads = 0;
        idle.by_data_server.clear();
        idle.by_cms_site.clear();
        PutFile(prom_remote_path_, EncodePrometheusText(idle));
    }
}

}  // namespace xrdhover
