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
