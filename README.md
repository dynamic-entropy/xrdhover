# xrdhover

XRootD (`root://`) held-rate reader for CMS / WLCG data challenges.
Formerly **xrd-readgen**.

This is the **inner hold process** for [tckestrel](https://github.com/dynamic-entropy/tckestrel):
one process per Condor job, token-bucket capped, metrics on a Pushgateway.
tckestrel owns Condor, Rucio, filelists, and the outer fleet loop.

All traffic is tagged `XRD_APPNAME=xrdhover/<version>` so server-side
monitoring (MONIT) can identify it.

## Install (linux/amd64)

From [GitHub Releases](https://github.com/dynamic-entropy/xrdhover/releases).
The `linux-amd64` tarball is an **AlmaLinux 9** build (glibc 2.34, OpenSSL 3).
That is the CMS Connect / tckestrel WN (`+REQUIRED_OS=rhel9`). Requires
`xrootd-client` 6 (`libXrdCl.so.6`) on the host, or CMSSW cmsenv on the grid.

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

**ABI stack for Condor** (all three must be the same OS family):

| Layer | Pin |
|---|---|
| Glidein | `+REQUIRED_OS=rhel9` (el9) |
| Binary | `linux-amd64` = AlmaLinux 9 + XRootD 6. Do not ship an el10 build. |
| `libXrdCl.so.6` | `run_xrdhover.sh` cmsenv of `CMSSW_20_1_0_pre2` (`el9_*`, cmsdist 6.0.2) |

CMS glideins do not ship XrdCl. Do not use CVMFS for the **executable**. Do not
cmsenv a 15.x–20.0 release (XRootD 5) or an el8 `SCRAM_ARCH` on an el9 WN
(`libssl.so.1.1`). Version table:
[tckestrel/docs/xrdhover.md](https://github.com/dynamic-entropy/tckestrel/blob/master/docs/xrdhover.md#cmssw--xrdcl).

```text
executable           = run_xrdhover.sh
transfer_executable  = true
arguments            = -C CMSSW_20_1_0_pre2 -- run job.json
transfer_input_files = job.json, files.txt, xrdhover
```

Preflight on the schedd (no XRootD I/O):

```sh
xrdhover validate job.json
```

Field mapping, `run_id` / `job_id` rules, and `max_bytes` policy live in
[tckestrel/docs/xrdhover.md](https://github.com/dynamic-entropy/tckestrel/blob/master/docs/xrdhover.md).
Schema is `schema_version: 1`. tckestrel writes `pattern.read_size` from
`chunk_bytes` (one `Read()`, max 8 MB) and `pattern.max_bytes` from
`max_bytes` (bytes from one file, default 32 MB). Do not use `"auto"`
and do not set `max_bytes` to a PREMIX file size.

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
`xrdhover_achieved_rate_bytes` (bytes / wall; cumulative) against
`xrdhover_target_rate_bytes` (token-bucket refill). Per-Read RTT is
`xrdhover_read_op_seconds` (issue→complete; token wait is not included).
The link label is
`src_dst` (`SOURCE__DEST`). Pushgateway grouping is `src_dst` + `replica`
(`sinks.job_id`), not `instance`. `replica` exists so N jobs on one link do
not overwrite each other; Grafana sums it away (not a variable or legend).
See [include/xrdhover/push_group.hh](include/xrdhover/push_group.hh).
The process DELETEs its Pushgateway group on exit unless
`sinks.pushgateway.keep` is true.

Import [dashboards/xrdhover-d1.json](dashboards/xrdhover-d1.json) into Grafana.
See [dashboards/README.md](dashboards/README.md).

## Build / test / release

Requires CMake ≥ 3.24, C++17, **XRootD 6** client libraries, libcurl, OpenSSL.

The GitHub `linux-amd64` release is produced with `ALMA_VERSION=9`. CI unit-tests
el9 and el10. Building on el10 (`ALMA_VERSION=10`) is fine locally; do not
upload that tarball as `linux-amd64` for tckestrel.

```sh
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
ALMA_VERSION=9 ./scripts/build-release.sh   # → dist/xrdhover-*-linux-amd64.tar.gz
```
