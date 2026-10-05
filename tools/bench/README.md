# UI baseline harness (WP-0)

`ui_bench.py` runs real probes three or more times and emits one JSON schema.
It requires explicit metrics for each scenario; missing values and failed
commands are errors. This prevents an unavailable probe from looking like a
fast result. For example:

```sh
python3 tools/bench/ui_bench.py \
  --command 'open=python3 tools/editor_open_probe.py --json /tmp/open.json; cat /tmp/open.json' \
  --runs 3 --out build/ui-bench.json
```

The existing native probe should be wrapped by a small adapter that maps its
`factory_ms`/`first_present_ms` fields to `open_ms`/`first_frame_ms`.
`--self-test` includes a planted missing-metric negative control.

For the existing Cocoa open probe, adapt its JSON with:

```sh
--command 'open=python3 tools/bench/editor_open_adapter.py build/open-{run}.json'
```

The adapter validates every row in `opens[]` and emits `warm_open_ms_max` and
`warm_first_frame_ms_max` alongside the selected `open_ms` row. Select a
different row explicitly with `--open-index N`; the default is the cold first
open. A missing or nonnumeric timing in any row fails the adapter instead of
silently dropping a warm-open result.

For frame cadence, run `frame_cadence_probe.py --json-out build/frame.json` and
adapt with `python3 tools/bench/frame_cadence_adapter.py build/frame.json`.
The adapter rejects reports without a gesture p95 metric.

## Hosted WP-0 receipt

Before publishing a hosted baseline, combine the three-or-more-run UI receipt
with the native importer/runtime receipt through the fail-closed adapter:

```sh
python3 tools/bench/wp0_hosted_receipt.py \
  build/ui-bench.json build/native/wp0-baseline.json \
  --negative-log build/native/wp0-negative.log \
  --out build/wp0-hosted.json
```

The adapter requires all six workload families (`open`, `frame`, `bridge`,
`layout`, `paint`, and `size`), matching run counts of at least three, and a
positive `rss_kb` value for every run in every family. The measured workload
values must also be positive; a zero bridge, layout, or paint value is an
unavailable probe, not a valid fast result. `ui_bench.py` measures RSS with one
`/usr/bin/time` envelope per child, so a large earlier child cannot satisfy a
later run through cumulative `RUSAGE_CHILDREN.ru_maxrss`.

It then delegates the native receipt to `wp0_baseline_adapter.py`, so host-size,
RGBA/layout byte, provenance, and planted offscreen-control checks remain
fail-closed. A missing family, fewer than three runs, or zero RSS is a planted
negative control and must reject before the receipt can be treated as a hosted
baseline. Both input receipts must carry the same identity object: hosted `AU`
or `VST3` format, host/build/run identifiers, exact product and SDK source
SHAs, an absolute artifact path, and an artifact digest and byte count computed
from the actual file or bundle directory. Standalone receipts and identities
that do not match their artifact are rejected. The native receipt must
explicitly set `hosted_capture: true`; a plain native-shot receipt cannot be
promoted to hosted evidence. `--negative-log` is required so every
authoritative output proves the planted offscreen control was actually run.
