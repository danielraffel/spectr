# Spectr Forge adapter receipt

**Receipt timestamp:** 2026-10-08  
**Consumer source head:** `2ddccee98a51773f6b9e2a3bcbc14aadcfeb65a4`  
**Pulp SDK source head:** `78b214f871f73a9842f80e140a887aff2de24609`  
**SDK provenance SHA-256:**
`65e1d0699fb8813f83e377348d3776e9c2bfe7578863f832249754a5a44c0ac4`

This is an opt-in owner adapter proof against the merged Pulp SDK. The SDK
used for this local graph test is a development build
(`distribution_eligible: false`); this receipt does not claim release signing,
notarization, or distribution eligibility.

## Adapter boundary

Spectr owns `SpectralMaskProcessor`, its controls, renderer/provider policy,
and CPU fallback. The adapter only exposes
`spectr::forge_adapter::create_processor()` and the existing `Processor`
through `SignalGraph::add_processor_node`. Forge owns graph placement and
routing. No Spectr DSP, provider, or control registry is copied into Forge.

The adapter is enabled only with `SPECTR_BUILD_FORGE_ADAPTER=ON`; it is not a
default product or catalog registration. The adapter's locally built test
executable was
`build-forge-adapter-merged/Spectr-forge-adapter-test` with SHA-256
`9421fd3bf1d933a3913eb29fba6bdc9d4faba4ddbeb3fd915a9985b2ce4c8a9d`.

## Configure and test

```sh
cmake -S . -B build-forge-adapter-merged -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR=/Volumes/Workshop/Code/pulp-sdk-merged-78b214f871f73a9842f80e140a887aff2de24609/lib/cmake/Pulp \
  -DSPECTR_BUILD_FORGE_ADAPTER=ON \
  -DBUILD_TESTING=ON

ctest --test-dir build-forge-adapter-merged \
  -R '^Spectr Forge adapter|^Spectr Forge adapter reports' \
  --output-on-failure
```

Result: **4/4 passed** in 0.12 seconds.

The cases prove product `Processor` creation, ordinary control declaration,
runtime `SignalGraph` `ProcessorNode` installation and audio rendering against
a separately constructed CPU `HeadlessHost` oracle. The GPU capability case is
a typed negative: when the provider is unavailable, Spectr reports no delivery
instead of claiming GPU execution. A provider-capable host must expose the
capability report and delivery surface, but this development-SDK receipt does
not claim a hardware GPU run.

## Scope

The receipt proves the owner adapter seam and CPU graph path. It does not claim
a Forge catalog row, a default-on Forge integration, GPU delivery, or a signed
Spectr adapter artifact. Those require the Forge consumer's own exact SDK
build, host acceptance, and distribution receipts.

