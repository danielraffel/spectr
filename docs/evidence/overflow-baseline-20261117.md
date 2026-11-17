# Overflow toolbar baseline qualification

The 1.0.11 SDK update exposed a product metadata omission: the native-only
optical correction preserved the 13px captured run rectangle and Menlo glyph
face, but discarded the primary font metrics of the captured cascade.
Inferring descent from Menlo (2px) placed the baseline at 17.5px label-local.

Chrome 156.0.8078.80 on macOS independently measures the actual shipping
materialized document at DPR 1 and 2. CDP reports Menlo-Regular as the single
glyph supplier. A zero-size inline-block marker in the text's inline formatting
context measures baseline y=836 for the unchanged Range rectangle y=826,
height=13: ascent 10, descent 3. Both pre/post probe Range rectangles are
identical. The primary JetBrains Mono cascade's run metrics therefore differ
from the fallback glyph face's own 9/2 metrics. This is a run rectangle, not
the CSS `line-height:10px` box, so adding half-leading again is also incorrect.

The native correction now passes the measured 10/3 metrics through the existing
SDK API for all three possible text target paths. Its existing +0.5px optical
shift is retained: local line top 6, frame origin 820.5, offset 13-3=10,
`floor(820.5 + 6 + 10 + .5) - 820.5 = 16.5` label-local. Both native optical
centering canaries assert that result. The former 15.834961/16 expectations
were based on native font ink rather than an independent browser baseline;
the proposed 17.5 expectation was also incorrect and has been repaired.

Reproduce from this repository on macOS with Menlo installed:

```sh
node tools/materialized_runtime_chromium_oracle.mjs \
  --artifact native-ui/materialized/materialized-document.runtime.json \
  --fixture test/fixtures/spectr-parity-analyzer-v1.json \
  --out /tmp/spectr-toolbar-baseline-new-proof --dpr 2 --toolbar-baseline
```

Use a new output directory; repeat with `--dpr 1`. The checked-in JSON receipts
bind the browser artifact hash, actual browser revision, resolved font, geometry
and measured baseline. The oracle rejects a different font, changed text run
geometry or changed 10/3 split. The native N1/frozen-atlas canaries remain a
separate check of the product optical correction and SDK frame-grid placement.
`tools/patch_materialized_overflow_line_metrics.py` replays the native fix and
is idempotent. No generic framework change or SDK byte patch is required.

This browser baseline evidence does not qualify installed Linux/Intel editor
interaction or release acceptance. Earlier packages from source
52b0b5e3155c523d0c4194a8e72fa6e909f03048 are superseded by this runtime repair
and must be rebuilt before publication.
