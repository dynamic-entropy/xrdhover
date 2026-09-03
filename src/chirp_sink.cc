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
#include <sys/wait.h>
#include <unistd.h>

namespace xrdhover {
namespace {

namespace fs = std::filesystem;

constexpr int kChirpTimeoutS = 10;

// Fork+exec condor_chirp with the given arguments.  Returns true on exit 0.
// Kills the child after kChirpTimeoutS seconds if it hasn't exited.
bool ForkExecChirp(const std::string& binary, const char* const argv[], int argc) {
    const pid_t pid = fork();
    if (pid < 0) {
        XRDHOVER_LOG_ERR("chirp: fork failed: %s", std::strerror(errno));
        return false;
    }
    if (pid == 0) {
        // CMS glidein condor_chirp is a Python script (htchirp).  CMSSW
        // puts Python 3.12 on PATH, which removed inspect.getargspec and
        // breaks htchirp.  Restore the pre-cmsenv PATH so the glidein's
        // Python is found instead.
        if (const char* orig = std::getenv("XRDHOVER_ORIG_PATH"); orig && *orig)
            setenv("PATH", orig, 1);
        if (const char* orig = std::getenv("XRDHOVER_ORIG_PYTHONPATH"); orig && *orig)
            setenv("PYTHONPATH", orig, 1);
        else
            unsetenv("PYTHONPATH");
        if (const char* orig = std::getenv("XRDHOVER_ORIG_PYTHONHOME"); orig && *orig)
            setenv("PYTHONHOME", orig, 1);
        else
            unsetenv("PYTHONHOME");

        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, 1); dup2(devnull, 2); close(devnull); }

        const char* av[argc + 2];
        av[0] = binary.c_str();
        for (int i = 0; i < argc; ++i) av[i + 1] = argv[i];
        av[argc + 1] = nullptr;
        execv(binary.c_str(), const_cast<char* const*>(av));
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
