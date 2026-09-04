# Grafana dashboards

Importable JSON for ops Grafana (xrdmon / CMS). The observability stack is
**not** shipped here — only dashboard definitions.

| File | Story |
|---|---|
| [`xrdhover-dc27-pushgateway.json`](xrdhover-dc27-pushgateway.json) | **Pushgateway** (`job=xrdhover`): achieved throughput by source–dest (with target overlay), success rate, inflight vs max, hard/soft errors, open/TTFB, Read-op RTT, bytes/CPU-sec, CMS-site attribution, FileSessions **rate** |
| [`xrdhover-dc27-chirp.json`](xrdhover-dc27-chirp.json) | **Chirp / Alloy** (`job=integrations/unix`): same panels. Freshness is `last_over_time(xrdhover_push_time_seconds[5m]) < 300` joined on `job_id` |

**Hard vs soft:** `xrdhover_errors_total` = failed sessions;
`xrdhover_soft_faults_total` = XrdCl Error log lines (e.g. connection reset)
even when the session still completes.

## What Prometheus is for

The live view is a **link** (`src_dst` = `SOURCE__DEST`): hold rate, inflight,
errors, latency. CMS sitename is the only extra dimension — “did this Open
land at the source site?” Disk hosts (`st-096-…:1095`) stay in `result.json`
(`by_data_server`). They are not Prometheus series.

| Layer | Metrics | Labels beyond the link |
|---|---|---|
| Link | `xrdhover_bytes_read_total`, `xrdhover_sessions_total`, `xrdhover_achieved_rate_bytes`, `xrdhover_target_rate_bytes`, `xrdhover_inflight_reads`, `xrdhover_max_inflight`, errors, histograms | none |
| CMS site | `xrdhover_site_bytes_total`, `xrdhover_site_sessions_total`, `xrdhover_site_achieved_rate_bytes` | `cms_site` (`unmapped` when sitename is missing) |

The Inflight panel is live `xrdhover_inflight_reads` vs configured
`xrdhover_max_inflight`. Legend last and max cover the selected window.

## Rates

**Bytes / hold:** use the gauges `xrdhover_achieved_rate_bytes` and
`xrdhover_site_achieved_rate_bytes` (`bytes / wall`). Do **not** prefer
`rate(*_bytes_total)` — Pushgateway scrapes make PromQL `rate()` noisy when a
series idles.

Grafana 12 treats each Prom series as a **data stream**. Achieved rate and
Target rate stats are range-only `sum()` (one number). Throughput Query B
is that same target sum (one dashed hold line). Query C is achieved sum.
Query A is per-link achieved so you can see which `src_dst` is at 0.
Do not use legend calc `sum` — that integrates one stream over time.

A live job with `target_rate` 200 Mbps and `achieved_rate` 0 is still in
the target sum and is a 0 slice of achieved. That is payload, not Grafana.

**FileSessions:** plot `rate(xrdhover_sessions_total[2m])` (link) and
`rate(xrdhover_site_sessions_total[2m])` (CMS site). Those counters have
stable labels (`src_dst`, `cms_site`, `result`), so `rate()` is the sessions/s
the token bucket is chewing — not a lifetime staircase, not live inflight.
Inflight is `xrdhover_inflight_reads`.

The achieved-throughput panel overlays `xrdhover_target_rate_bytes` (dashed)
next to `xrdhover_achieved_rate_bytes` (`bytes / wall`) so the hold can be
compared to the token-bucket refill. Series are labeled `src_dst`
(`SOURCE__DEST`).

Read-op RTT is `xrdhover_read_op_seconds` (issue→complete per Read; not ICMP).
TTFB is first Read submit → first byte (token-queue wait is not included).

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

| PromQL | Pushgateway (`xrdhover-dc27-pushgateway.json`) | Chirp (`xrdhover-dc27-chirp.json`) |
|---|---|---|
| gauges | `last_over_time(xrdhover_*[5m])` then freshness `and` | same |
| freshness | `and on (job, src_dst, job_id)` `(time() - last_over_time(xrdhover_push_time_seconds[5m])) < 300` | same |
| display | `sum by (source, dest, src_dst)` | same |
| `job` variable | pinned `xrdhover` | pinned `integrations/unix` |
| uniqueness label | Pushgateway grouping `replica` (PUT URL only; not the join key) | metric label `job_id` (not a Grafana dimension) |

Do **not** mix the two paths on one dashboard. Dual-sink jobs write both; Grafana would double-count if `$job` were `.*`.

After deploying a new binary, wipe stale Pushgateway groups (last PUT
otherwise keeps old metric names until DELETE):

```text
DELETE https://xrdprom.cern.ch:2094/metrics/job/xrdhover
```

Gauge panels only draw a group that is **fresh**: encode time
(`xrdhover_push_time_seconds`) younger than **300s**. Both the gauge and
the push timestamp use `last_over_time(...[5m])`. Freshness on
`push_time` alone is not enough — an instant gauge selector still drops
that job from the `sum` when one scrape is missed (Target 600→500 Mbps
for a single 15s step). `or vector(0)` is only the all-jobs-idle floor;
it is not those one-job dips. After the last snapshot ages out (or
`.prom` removed / PUT DELETE), series go to 0.
Do **not** join on Pushgateway `push_time_seconds` / `replica` — the
gauges are labeled `job_id`; that join is empty even when PUTs are live.
300s is past `sinks.snapshot_interval` (15s Pushgateway-only, **30s**
when chirp is set), chirp-stretch on the shared timer thread, and scrape
(keep scrape at 15s). On clean exit chirp **removes** the `.prom` file
(do not leave a zeroed target).
`--persistence.interval` on Pushgateway is how often it fsyncs disk; it is not
the scrape interval — do not set scrape to 15m to “match” it.
xrdhover DELETEs its Pushgateway group on a clean exit; `condor_rm` / crash leaves the
last PUT. Freshness drops those after 300s. Stat queries use
`… or vector(0)` at each step and Grafana calc **last** (not lastNotNull):
a range window would otherwise keep the last live rate for the whole
dashboard interval. Throughput `spanNulls` is off so a dead series does not
draw through to now.

## Import

1. Grafana → **Dashboards → New → Import** → upload `xrdhover-dc27-pushgateway.json` (Pushgateway) and `xrdhover-dc27-chirp.json` (chirp).
2. Select the Prometheus datasource that scrapes xrdprom (Pushgateway) **and** receives Alloy `remote_write` (chirp).
3. Pushgateway dashboard: `job` is pinned to `xrdhover`. Chirp dashboard: `job` is pinned to `integrations/unix`. Then `source`, `dest`, and `src_dst` (`SOURCE__DEST` link). Do **not** add `replica` or `job_id`. Every timeseries `sum by (source, dest, src_dst)` (plus the panel key: class, kind, cms_site, result). Stats and the Total line `sum()` the same filter. Filter by source or dest alone, or both.

## Generator side

```bash
./dev/local-server.sh -b
./build/xrdhover run workloads/example.json --skip-auth-check
```

Set `sinks.pushgateway.url` to `https://xrdprom.cern.ch:2094`. Grafana on xrdmon scrapes that Pushgateway via xrdmon Prometheus.

Soft-fault counting requires XrdCl Error logs (default / `XRD_LOGLEVEL=Info`).
Site panels fill from live `query config sitename` (deferred off the I/O path;
cached per DataServer). Optional `--site-map` is fallback only. Unmapped
disks export `cms_site="unmapped"` so site series still sum to the link.

Prefer `xrdhover_achieved_rate_bytes` (gauge): cumulative `bytes_read / elapsed`
on the generator’s `steady_clock`. Rate panels use Grafana unit `bps` (SI
**bits/s**). Prom gauges are bytes/s; panel queries multiply by 8. CLI `--rate`
and JSON `target_rate` accept SI bits only (`1Gbps`, `17Mbps`).

Defaults that bound stuck peers: `session_timeout` 60s, `connection_window` 15,
`connection_retry` 2 (XrdCl ConnectionWindow default is 120).

Pushgateway-only: `--snapshot-interval` / `sinks.snapshot_interval` **15s**
(≤ scrape). Chirp: **30s** (ClassAd + `condor_chirp put` share that clock;
Alloy scrape stays 15s and repeats the last file). Ground truth:
`results/<run_id>/result.json`.
