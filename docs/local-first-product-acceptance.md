# Local-first M5 product acceptance

`Spectr M5 Product Acceptance` is a manually dispatched exact-head gate. It
produces a Release PKG for hands-on M5 testing and never runs automatically on
a push or pull request.

## Capacity and isolation

Shipyard chooses capacity from the local M1/M3/M5 macOS ARM64 pool using these
literal labels:

```text
self-hosted, macOS, ARM64, spectr-build, spectr-build-vm, spectr-gate-fast
```

The labels select a transient clean Tart guest. The workflow accepts no runner
provider or selector input, has no GitHub-hosted lane, names no physical Mac,
and builds only below the guest's unique `$RUNNER_TEMP`. Do not replace that
with a warm repository build directory or static runner name.

The current authorities are Pulp's `.agents/skills/ci/SKILL.md`,
`.agents/skills/tart-ci/SKILL.md`, and `tools/scripts/runner_topology.json`, plus
the installed TartCI `spectr-build-vm` and `spectr-gate-fast` profiles.
Historical planning notes apply only where they agree with those sources.

## Released SDK contract

[`tools/ci/pulp-sdk-release.json`](../tools/ci/pulp-sdk-release.json) pins the
immutable Pulp release tag, full source commit SHA, Darwin ARM64 asset, and its
SHA-256. The workflow verifies the archive before extraction, configures
`SPECTR_EXPECTED_PULP_SDK_SHA`, and checks the SDK's positive provenance,
Release marker, platform, feature policy, build-info header, and exported target
surface. A compatible version number or unmarked local SDK is insufficient.

## Gate and receipt

Every expensive step names the **Spectr M5 product-acceptance gate** it unlocks.
The lane builds Release Standalone, AUv2, VST3, and CLAP products, runs focused
native/editor/parameter/artifact tests, validates AUv2, and packages those exact
artifacts as an unsigned acceptance PKG. It deliberately does not rerun the
entire test corpus.

Pull requests from forks do not run the gate. The lane's TartCI VM is ephemeral
but mounts the host's shared ccache and configure-check cache writable, so fork
code is kept off it, and there is no GitHub-hosted fallback. The fork PR's
skipped job is named `Spectr M5 product-acceptance gate (fork PR, not run)`, so
the required check stays unreported; after review, a maintainer runs the gate
on a same-repo branch carrying that head.

The artifact name includes the exact GitHub SHA. Confirm the candidate head
before dispatch, and reuse an existing receipt for that SHA rather than running
a duplicate.
