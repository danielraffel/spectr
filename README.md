# Spectr

A zoomable frequency-slicer audio effect built on [Pulp](https://github.com/danielraffel/pulp).

Spectr is not an EQ and not a spectrum analyzer. It is a **precision tool
for isolating, removing, and recombining narrow frequency-defined parts
of a sound** with unusual depth and targeting.

## Status

Planning complete. V1 effect implementation is queued behind
[`danielraffel/pulp#625`](https://github.com/danielraffel/pulp/issues/625),
which adds the supplemental plugin-state capability Spectr uses for
host/session recall of variable band layouts and snapshot banks. Route-
agnostic foundation work is in progress while `#625` lands.

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
