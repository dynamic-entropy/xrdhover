# xrdhover

XRootD (`root://`) held-rate reader for CMS / WLCG data challenges.
Formerly **xrd-readgen**.

This is the **inner hold process** for [tckestrel](https://github.com/dynamic-entropy/tckestrel):
one process per Condor job, token-bucket capped, metrics on a Pushgateway.
tckestrel owns Condor, Rucio, filelists, and the outer fleet loop.

All traffic is tagged `XRD_APPNAME=xrdhover/<version>` so server-side
monitoring (MONIT) can identify it.

## Install (linux/amd64)

From [GitHub Releases](https://github.com/dynamic-entropy/xrdhover/releases)
(requires `xrootd-client` on the host):

```sh
curl -fsSL https://github.com/dynamic-entropy/xrdhover/releases/latest/download/install.sh | sudo bash
xrdhover version
```

| Path | Role |
|---|---|
| `/usr/local/bin/xrdhover` | binary |
| `/etc/xrdhover/` | example workloads and filelists |
| `/var/lib/xrdhover/results` | FileSink output |

## Condor / tckestrel

tckestrel fetches the release binary (`<version>/<arch>/xrdhover`) and submits
it as the job executable (not CVMFS):

```text
executable           = xrdhover
transfer_executable  = true
arguments            = run job.json
transfer_input_files = job.json, files.txt
```

Preflight on the schedd (no XRootD I/O):

```sh
xrdhover validate job.json
```

Field mapping, `run_id` / `job_id` rules, and `max_bytes` policy live in
[tckestrel/docs/xrdhover.md](https://github.com/dynamic-entropy/tckestrel/blob/master/docs/xrdhover.md).
Schema is `schema_version: 1`. Do not use `pattern.max_bytes: "auto"` for
PREMIX — auto caps a session at 32 MB.

A grid-shaped example is [workloads/job.json](workloads/job.json).

## Local run

```sh
# terminal 1: throwaway local server (creates a 256 MiB test file)
dev/local-server.sh

# terminal 2
cmake -S . -B build && cmake --build build -j
build/xrdhover read root://localhost:10945//tmp/xrdhover-data/test-256M.bin
build/xrdhover run --endpoint root://localhost:10945/ \
  --filelist filelists/local.txt --duration 30s --rate 400Mbps \
  --max-bytes 32MiB
# or: build/xrdhover run workloads/example.json --skip-auth-check
```

tckestrel does not emit a flag line.

## Exit codes

tckestrel treats these as the circuit-breaker contract:

| Code | Meaning |
|---|---|
| 0 | OK (or some sessions failed but at least one succeeded) |
| 1 | All sessions failed |
| 2 | Auth / config / empty filelist / usage |

Workload `run` checks the x509 proxy: not group/other-writable, remaining TTL
≥ duration + 300s.

## Metrics

Pushgateway job default is `xrdhover`. Scrape
`xrdhover_achieved_rate_bytes` (bytes / wall; cumulative). Labels:
`run_id` (stable per source–dest cell), `job_id` (unique per process).
The process DELETEs its Pushgateway group on exit unless
`sinks.pushgateway.keep` is true.

Import [dashboards/xrdhover-d1.json](dashboards/xrdhover-d1.json) into Grafana.
See [dashboards/README.md](dashboards/README.md).

## Build / test / release

Requires CMake ≥ 3.24, C++17, **XRootD 6** client libraries, libcurl, OpenSSL.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
./scripts/build-release.sh   # → dist/xrdhover-*-linux-amd64.tar.gz
```
