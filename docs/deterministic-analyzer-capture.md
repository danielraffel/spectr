# Deterministic analyzer capture

The fixture is test-only. It supplies source FFT bins, a stable seed, epoch,
sequence, and canonical SHA-256 so browser and native capture lanes can name
the same input independently of host audio and animation timing.

## Browser

```sh
node test/test_deterministic_analyzer_fixture.mjs
node tools/deterministic_analyzer_fixture.mjs \
  --fixture test/fixtures/deterministic-analyzer-v1.json --plant-invalid
node test/test_editor_analyzer_browser.mjs resources/editor.html \
  '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' \
  --analyzer-capture \
  --analyzer-fixture test/fixtures/deterministic-analyzer-v1.json \
  --analyzer-screenshot artifacts/deterministic-capture/browser-peaks.png \
  --analyzer-receipt artifacts/deterministic-capture/browser-receipt.json
```

The capture lane waits for the injected peak frame and validates its identity
before taking the screenshot. It stops before the full interaction oracle's
intentional silence frame and teardown. Other modes retain their existing
behavior. Receipt output from modes that never consume the fixture is rejected.
A Chromium teardown watchdog remains a failing process; its receipt records
that limitation even if the in-page oracle finished.

The configured CTest rows are `Spectr-deterministic-analyzer-fixture` and
`Spectr-browser-deterministic-analyzer-capture`.

## Native

First validate the immutable source file using the shared tool, then use the
same file with the native screenshot harness:

```sh
node tools/deterministic_analyzer_fixture.mjs \
  --fixture test/fixtures/deterministic-analyzer-v1.json --consumer native
build/Spectr-native-shot --backend=skia --analyzer-only \
  --out=artifacts/deterministic-capture/native \
  --analyzer-fixture=test/fixtures/deterministic-analyzer-v1.json \
  --analyzer-receipt=artifacts/deterministic-capture/native-receipt.json
```

The native harness verifies frame acceptance and captures the editor. Its
`fixture_sha256_verification` field explicitly records that it copies the
manifest hash; canonical hash validation belongs to the shared Node tool.
Native calculations use float precision, so this contract establishes shared
source bins and frame identity, not bit-identical projected traces or pixel
parity. No production behavior changes when the fixture flags are omitted.

## Current evidence and limit

The two configured CTest rows passed locally on 2026-10-08. The browser
screenshot shows the two supplied peaks. Node controls reject negative
sequence numbers, invalid geometry, nonfinite/out-of-range bins, non-test
provenance, and fixture content changed without its canonical hash.

The changed native screenshot translation unit compiled successfully. Full
native linkage and capture remain unverified: the locally available SDK from
Pulp source `c3cc0a81b062208fe7c636a70181302796406aa4` fails compilation of the
unchanged `src/ui/native_editor.cpp` because `pulp::view::Label` is not declared
at that source's include boundary. No native receipt or browser/native visual
parity is claimed by this change. A compatible SDK/build and a separate native
capture remain required.
