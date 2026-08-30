# Grafana dashboards

Importable JSON for ops Grafana (xrdmon / CMS). The observability stack is
**not** shipped here — only dashboard definitions.

| File | Story |
|---|---|
| [`xrdhover-d1.json`](xrdhover-d1.json) | Achieved throughput by source–dest, success rate, inflight, hard/soft errors, open/TTFB, bytes/CPU-sec, achieved rate + FileSessions by CMS site and DataServer |

**Hard vs soft:** `xrdhover_errors_total` = failed sessions;
`xrdhover_soft_faults_total` = XrdCl Error log lines (e.g. connection reset)
even when the session still completes.

Site/server throughput uses the same definition as the overall panel: gauges
`xrdhover_site_achieved_rate_bytes` /
`xrdhover_endpoint_achieved_rate_bytes` (`bytes / wall`). Do **not** prefer
`rate(*_bytes_total)` — Pushgateway scrapes make PromQL `rate()` noisy when a
label is idle. FileSessions panels count completed Open→…→Close work items,
not TCP connections.

The achieved-throughput panel does not overlay `xrdhover_target_rate_bytes`.
The Target rate stat stays on the side. Series are labeled `src_dst`
(`SOURCE__DEST`).

## Pushgateway `replica` (not a dashboard dimension)

tckestrel holds a cell rate `R` with `N` jobs at `R/N`. Those jobs share
`run_id` / Prom `src_dst` (`SOURCE__DEST`). Pushgateway last-PUT-wins per
grouping key, so the URL is:

```text
/metrics/job/xrdhover/src_dst/<SOURCE__DEST>/replica/<job_id>
```

`replica` is `sinks.job_id` (`SOURCE__DEST__k` when `N > 1`). It exists only
so those N PUTs do not clobber each other. Without it, Target rate is one
`job_rate`, not `R` — a 10 Gbps hold can show ~5 Gb/s.

**Do not surface `replica` in Grafana.** No template variable, no
`{{replica}}` legend, no `sum by (…, replica)`. Canonical contract:
[include/xrdhover/push_group.hh](../include/xrdhover/push_group.hh).

| PromQL | Labels | Why |
|---|---|---|
| freshness | `and on (job, src_dst, replica)` | a live sibling must not keep a `condor_rm` leftover |
| display | `sum by (source, dest, src_dst)` | N jobs → one `SOURCE__DEST` series |
| variables | `job`, `source`, `dest`, `src_dst` | never `replica`, never `job_id` |

After deploying a binary that adds `replica`, wipe groups that lack it:

```text
DELETE https://xrdprom.cern.ch:2094/metrics/job/xrdhover
```

Gauge panels only draw a group when `push_time_seconds` is younger than 60s.
A `condor_rm` leaves the last Pushgateway PUT in place (no DELETE); without
that filter the Achieved-rate Value stays at the last number. Wait one scrape
after the wipe.

## Import

1. Grafana → **Dashboards → New → Import** → upload `xrdhover-d1.json`.
2. Select the Prometheus datasource that scrapes the Pushgateway.
3. Variables: `job` (default `xrdhover`), `source`, `dest`, and `src_dst`
   (`SOURCE__DEST` link). Do **not** add `replica`. Every timeseries
   `sum by (source, dest, src_dst)` (plus the panel key: class, kind,
   cms_site, …). Stats and the Total line `sum()` the same filter. Filter by
   source or dest alone, or both.

## Generator side

```bash
./dev/local-server.sh -b
./build/xrdhover run workloads/example.json --skip-auth-check
```

Set `sinks.pushgateway.url` to `https://xrdprom.cern.ch:2094`. Grafana on xrdmon scrapes that Pushgateway via xrdmon Prometheus.

Soft-fault counting requires XrdCl Error logs (default / `XRD_LOGLEVEL=Info`).
Site panels fill from live `query config sitename` (deferred off the I/O path;
cached per DataServer). Optional `--site-map` is fallback only.

Prefer `xrdhover_achieved_rate_bytes` (gauge): cumulative `bytes_read / elapsed`
on the generator’s `steady_clock`. Rate panels use Grafana unit `bps` (SI
**bits/s**). Prom gauges are bytes/s; panel queries multiply by 8. CLI `--rate`
and JSON `target_rate` accept SI bits only (`1Gbps`, `17Mbps`).

Defaults that bound stuck peers: `session_timeout` 60s, `connection_window` 15,
`connection_retry` 2 (XrdCl ConnectionWindow default is 120).

Use `--snapshot-interval` / `sinks.snapshot_interval` ≤ scrape interval
(often 15s). Ground truth: `results/<run_id>/result.json`.
