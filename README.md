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

Requires Pulp SDK 0.33.0+. Copy `pulp.toml.example` to `pulp.toml` and
edit the SDK paths for your environment, then:

```bash
pulp build
pulp test
```

## License

TBD.
