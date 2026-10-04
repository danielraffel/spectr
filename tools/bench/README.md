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
