# Spectr

A zoomable frequency-slicer audio effect built on [Pulp](https://github.com/danielraffel/pulp).

Spectr is not an EQ and not a spectrum analyzer. It is a **precision tool
for isolating, removing, and recombining narrow frequency-defined parts
of a sound** with unusual depth and targeting.

## Status

**Audio + state layer working** (M1–M4). `pulp#625` landed as PR#628 and Spectr's
supplemental plugin-state blob is live.

**Editor UI is parked** behind
[`danielraffel/pulp#651`](https://github.com/danielraffel/pulp/issues/651).
The plan is to embed the prototype HTML verbatim via `WebViewPanel` for
pixel-perfect visual parity; the blocker is that `View` subclasses inside
a plugin editor can't reach the `PluginViewHost`'s native NSView handle.
The full attempt lives on branch
[`feature/webview-editor-parked`](https://github.com/danielraffel/spectr/tree/feature/webview-editor-parked)
ready to resume once `#651` ships an accessor.

See [`planning/`](planning/) for the full design package:

- [`planning/Spectr-V2-Product-Spec.md`](planning/Spectr-V2-Product-Spec.md) — product contract
- [`planning/Spectr-V2-Pulp-Handoff.md`](planning/Spectr-V2-Pulp-Handoff.md) — build guidance
- [`planning/Spectr-V1-Build-Plan.md`](planning/Spectr-V1-Build-Plan.md) — implementation sequence
- [`planning/Spectr-Sampler-Phase-Spec.md`](planning/Spectr-Sampler-Phase-Spec.md) — Phase 4+ sampler spec
- [`planning/Spectr-Upstream-Integration-Plan.md`](planning/Spectr-Upstream-Integration-Plan.md) — Pulp pickup playbook
- [`planning/Spectr-Build-Signoff.md`](planning/Spectr-Build-Signoff.md) — current build clearance state

## Building

Requires Pulp SDK 0.72.2+. Copy `pulp.toml.example` to `pulp.toml` and
edit the SDK paths for your environment, then:

```bash
pulp build
pulp test
```

### Building Pulp from source for Spectr

Spectr links against an installed Pulp SDK at
`$HOME/.pulp/sdk/<version>/`. To build a local SDK from a Pulp tag or
commit:

```bash
git worktree add /tmp/pulp-vX.Y.Z vX.Y.Z
cd /tmp/pulp-vX.Y.Z
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$HOME/.pulp/sdk/X.Y.Z \
  -DPULP_BUILD_WEBVIEW=ON
cmake --build build -j8 --target install
```

> **Important — `-DPULP_BUILD_WEBVIEW=ON` is required.** Spectr's editor
> path uses `pulp::view::WebViewPanel::create` and
> `pulp::view::make_webview_embedded_resource_fetcher`. Pulp's CMake
> defaults `PULP_BUILD_WEBVIEW=OFF` (CLI-friendly default), so without
> this flag `libpulp-view.a` ships without the WebView symbols and
> Spectr fails to link with `Undefined symbols for architecture arm64`.
> Tracked upstream as [pulp#1351](https://github.com/danielraffel/pulp/issues/1351)
> — once the framework auto-detects or exports a `Pulp::WebView`
> component this note can be removed.

Then point Spectr at the install:

```bash
cmake -S . -B build \
  -DPulp_DIR=$HOME/.pulp/sdk/X.Y.Z/lib/cmake/Pulp \
  -DSPECTR_NATIVE_EDITOR=ON
cmake --build build --target Spectr_Standalone -j8
./run-native.sh   # NOT `open Spectr.app` — use the wrapper or absolute binary path
```

Why `run-native.sh` (or absolute binary path)? The standalone is built
with `CFBundleIdentifier=com.pulp.spectr` while the WebView build uses
`com.pulp.spectr-webview`. macOS Launch Services may resolve a bare
`open Spectr.app` to whichever bundle it sees first. The wrapper script
launches by path, eliminating the ambiguity.

## License

TBD.
