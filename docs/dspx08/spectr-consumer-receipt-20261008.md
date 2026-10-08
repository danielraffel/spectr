# DSPX-08 Spectr consumer receipt

**Receipt timestamp:** 2026-10-08 06:30 UTC  
**Consumer:** Spectr, branch `codex/dspx08-spectr-consumer-20261008`  
**Consumer source head:** `c860a3d05a09e7135c0d08905a7248fc44f05baf`  
**Pulp SDK:** `0.931.3`, source `v0.931.3` at
`68a48f899164056ac25691668138ec95cee86af9`  
**SDK provenance SHA-256:**
`e7638e63d4e72d9ddfa91ef0d1d6ba25f2629d92fbddce287ae835e06985a817`

This receipt records a real Spectr consumer of the Pulp GPU-audio transport. It
is deliberately scoped to the existing SpectralMaskProcessor /
SpectralFrameEngine path. It does not claim Forge catalog registration, a
`.pulpgraph` consumer, GPU-NAM support, or a default-on GPU renderer.

## Runtime path

Spectr's existing processor constructs the shared spectral renderer when the
following opt-in controls are enabled:

```text
SPECTR_EXPERIMENTAL_SHARED_RENDERER=ON
SPECTR_SHARED_PRODUCT_ACCEPTANCE=ON
SPECTR_SHARED_NATIVE_HOST_PROBE=ON
SPECTR_SHARED_SPECTRAL_FIXTURE=ON
SPECTR_GPU_AUDIO_PERF_FIXTURE=ON
```

The renderer uses Pulp's installed GPU-audio transport and preserves the
existing control authority, band publication, automation, mix, reset, PDC and
fallback path. The CPU renderer remains the default. A missing or fenced GPU
result selects the prepared CPU result with the same declared delay. The
opt-in path is therefore fail-closed and does not change saved mode state.

The exact production SDK configure was:

```sh
cmake -S . -B build-dspx08-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/Users/danielraffel/Code/pulp-dsp-next-20261001/sdk-v0931.3/pulp-sdk \
  -DSPECTR_EXPECTED_PRODUCT_GIT_SHA=c860a3d05a09e7135c0d08905a7248fc44f05baf \
  -DSPECTR_EXPECTED_PULP_SDK_SHA=68a48f899164056ac25691668138ec95cee86af9 \
  -DSPECTR_EXPERIMENTAL_SHARED_RENDERER=ON \
  -DSPECTR_SHARED_PRODUCT_ACCEPTANCE=ON \
  -DSPECTR_SHARED_NATIVE_HOST_PROBE=ON \
  -DSPECTR_SHARED_SPECTRAL_FIXTURE=ON \
  -DSPECTR_GPU_AUDIO_PERF_FIXTURE=ON \
  -DSPECTR_NATIVE_PREVIEW_IDENTITY=ON
```

## Positive and typed-negative proof

The exact installed-SDK Release build passed all 16 focused runtime tests:

- `Spectr-shared-product-acceptance`
- `Spectr-gpu-stft-baseline`
- `Spectr-gpu-stft-rejection-controls`
- `Spectr-shared-spectral-fixture`
- `Spectr-shared-spectral-partitions`
- `Spectr-shared-spectral-controls`
- `Spectr-shared-spectral-renderer`
- `Spectr-shared-spectral-scaling`
- `Spectr-shared-spectral-layers`
- `Spectr-shared-spectral-mix`
- `Spectr-shared-spectral-host-inventory`
- `Spectr-shared-spectral-trace-validator`
- `Spectr-shared-spectral-trace-accounting`
- `Spectr-shared-spectral-reset`
- `Spectr-gpu-audio-status-ui`
- `Spectr-gpu-audio-status`

Command and result:

```sh
ctest --test-dir build-dspx08-release --output-on-failure \
  -R '^(Spectr-shared-product-acceptance|Spectr-gpu-stft-(baseline|rejection-controls)|Spectr-shared-spectral-(fixture|partitions|controls|renderer|scaling|layers|mix|host-inventory|trace-validator|trace-accounting|reset)|Spectr-gpu-audio-status(-ui)?)$'
# 100% tests passed out of 16; total test time 29.64 sec
```

The shared host contract also passed both host probes:

```sh
ctest --test-dir build-dspx08-release --output-on-failure \
  -R '^Spectr-shared-(clap-host|host-contract)$'
# 100% tests passed out of 2; total test time 5.81 sec
```

The rejection controls exercise invalid or unavailable shared-renderer paths
and require typed fallback/rejection outcomes. They are not metadata checks:
they execute the renderer and transport controls. The trace accounting test
requires terminal delivery, fallback and cancellation accounting to balance;
the host inventory and trace validator checks reject malformed provider/trace
identity.

## Shipping binary boundary

A second Release build was configured against the same exact SDK with
`SPECTR_NATIVE_PREVIEW_IDENTITY=OFF`. Pulp's host loaded and processed all
three built plugin formats:

```sh
ctest --test-dir build-dspx08-shipping --output-on-failure \
  -R 'Pulp host loads and processes the built Spectr (CLAP|VST3|AU) artifact'
# 100% tests passed out of 3; total test time 1.01 sec
```

Payload SHA-256 values before packaging were:

| Payload | SHA-256 |
| --- | --- |
| AU | `3c6d3444e410d1b8c5c5a955ef3f69d901a9f30752d580dc28bd4843679611fd` |
| VST3 | `ecd91ccdef5e9054b8135571bf67741f6f80b54b7d6dbff6050db31c9c6864df` |
| CLAP | `7f80a778456ce53d8444385d350c8b0331ac59d6f4fb5119cbbd6dd756ea103d` |
| Standalone | `bb6e670f76fd91a49e5ca947900c873cc2d8787e0d8a5e6e562dd2c10d5192d6` |

## Unified controls and documentation

The product controls are exposed through the existing Spectr processor and
Pulp GPU-audio status path. The opt-in build controls above are CMake controls;
no new public ABI or parallel provider API was added. The existing control and
fallback behavior is documented in
[`docs/experimental-shared-spectral-product.md`](../experimental-shared-spectral-product.md),
including latency/PDC, lifecycle ownership, delivery accounting, reset,
forced-CPU negative control and the limits of the experimental path.

## Packaging disposition

The first package attempt at the consumer receipt's source head is retained as
historical negative evidence:
`artifacts-dspx08/Spectr-1.0.7.pkg` has SHA-256
`b97a6f66895c36ed9a8c4a218d898e1e03ba8429bc6b8e94200c8e016c7051cd` and its
post-package check exposed the nested Sparkle `Autoupdate` staging-validation
bug. Spectr PR [243](https://github.com/Generous-Corp/spectr/pull/243) fixed
that packaging path by signing source Sparkle helpers before staging and
checking the extracted stapled app after notarization.

The fixed exact-SDK Release package is
`/Users/danielraffel/Code/spectr-package-sparkle-20261008/artifacts-package-final/Spectr-1.0.7.pkg`
(SHA-256
`990fd3e7c4784bf48cfa0caf55c67c69495739dd62127fa3a7475a5e35a5385f`,
75,253,219 bytes). Apple notary submission
`2ca7dac3-a77c-49a7-a2e6-f4b619c74892` was `Accepted`; `xcrun stapler validate`
and `pkgutil --check-signature` passed, and the extracted stapled app plus
AU/VST3/CLAP passed `check_sparkle.py bundles --signed`. This is now a
notarized production package proof.

## Scope conclusion

**PASS:** a real Spectr production consumer path, exact SDK provenance, shared
runtime, unified controls, positive tests, typed-negative controls, and CLAP /
VST3 / AU host execution are proven on this branch.

**OPEN:** default-on GPU policy, a Forge graph/catalog consumer, DSPX-07
WAM/WebCLAP packaged parity, and GPU-NAM/Spectr model-provider work remain
outside this receipt. Those require separate architecture/provider evidence and must not be
inferred from this consumer proof.
