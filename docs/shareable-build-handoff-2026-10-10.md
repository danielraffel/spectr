# Spectr shareable build handoff — 2026-10-10

This handoff names artifacts that were actually produced and the host evidence
behind them. It does not turn blocked or unverified hosts into release claims.

## macOS Apple Silicon

Share this notarized installer:

```text
/Users/danielraffel/Code/spectr-artifacts-20261010/Spectr-1.0.7.pkg
```

```text
bytes: 75252717
sha256: 8ce46e0fe2928a4a194ee6d126f49554706f4bc68445e8929b1ce09e7f23e631
```

The package passed Apple notarization, stapling, `spctl --assess --type
install`, installer signature validation, and the package wrapper's Sparkle
checks. It contains the Standalone, AU, VST3, and CLAP products from the ARM64
Release build. Native AU validation passed with `auval -v aufx Spec Pulp`.

Receipts:

- `docs/macos-arm64-installer-receipt-2026-10-10.json`
- `docs/macos-arm64-auval-receipt-2026-10-10.json`

Install by opening the `.pkg` and follow the macOS Installer prompts. The
package is notarized for Gatekeeper on supported macOS versions (deployment
floor 13.4).

## Windows ARM64

The portable handoff package is:

```text
/Users/danielraffel/Spectr-arm64ec-headless-installer.zip
```

```text
bytes: 9769538
sha256: f956296ee83d81708e5c8ff7449abcc4e0533ad4fefe0e94cb349476bd1a0785
```

Extract it, review `manifest.json`, and run `Install-Spectr.ps1` from an
elevated PowerShell prompt. The installer verifies every manifest SHA-256
before copying the Standalone, VST3, and CLAP artifacts. This is a portable
PowerShell handoff, not a signed MSI or EXE installer.

UTM interactive validation is available from the Spectr checkout:

```bash
cd /Users/danielraffel/Code/spectr-windows-build-20261007
tools/windows/open-utm-desktop.sh
```

The helper starts the retained VM, repairs RDP, verifies REAPER ARM64 and
Ableton Live 10 Trial are installed, and opens the saved Jump Desktop
`pulp-win-ci` connection at `127.0.0.1:53390`. REAPER ARM64EC loaded Spectr,
rendered its controls, and produced a non-silent render.

## Windows x64

REAPER x64 proof is recorded in
`docs/windows-receipts-2026-10-10/proxmox-x64-reaper-render-receipt.json`.
It proves visible Spectr controls and non-silent audio through REAPER using
Dummy Audio. No signed x64 installer artifact is currently available.

## Known limits

- The current Pulp SDK has arm64-only Pulp runtime archives, so an Intel macOS
  build cannot be produced from this SDK. See
  `docs/macos-intel-validation-receipt-2026-10-10.json`.
- Ableton plugin loading is unverified. Live installation is blocked by the
  available guests' missing AVX/AVX2 support.
- LÖVE has no current Pulp/Spectr host adapter and is explicitly unsupported
  for this build. See `docs/love-host-compatibility-2026-10-10.md`.
