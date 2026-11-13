# AAX (Pro Tools)

Spectr can build an AAX plug-in for Pro Tools. AAX is optional and off by
default. A default configure, the M5 acceptance gate and the release workflow
all build without it and never need the Avid SDK.

## Policy

AAX is a vendor format under Avid's own license, so it follows Pulp's rule for
optional vendor SDKs:

- **Off by default.** Nothing builds AAX unless you configure with
  `-DPULP_ENABLE_AAX=ON`. A `PULP_AAX_SDK_DIR` in your environment does not
  opt in by itself.
- **You supply the SDK.** Download the AAX SDK from Avid under your own
  agreement and keep it outside this repository. A configure that points
  `PULP_AAX_SDK_DIR` inside the source tree is refused, and `.gitignore`
  catches the usual SDK directory names.
- **Nothing Avid- or PACE-owned is committed.** No SDK files, validator,
  `wraptool`, wrap configuration, iLok data or signing credentials go into
  this repository, its CI or its release assets.
- **CI never requires it.** The acceptance gate and the release workflow never
  enable AAX. `tools/ci/test_product_acceptance_contract.py` fails if either
  one starts to.

## Building

```bash
cmake -S . -B build-aax -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR="$PULP_SDK_ROOT/lib/cmake/Pulp" \
  -DPULP_ENABLE_AAX=ON \
  -DPULP_AAX_SDK_DIR="$HOME/SDKs/avid/aax-sdk/current"
cmake --build build-aax --parallel 4 --target Spectr_AAX
```

That builds `Spectr.aaxplugin` with the same native editor as the other
formats. `-DSPECTR_AAX_HEADLESS=ON` builds a processor-only AAX instead
(`SpectrAAXHeadless_AAX`). Use it only to isolate a load problem: it opens in
Pro Tools with an empty editor.

`cmake/SpectrAAX.cmake` decides which artifact a configuration builds. These
configurations stop at configure time with a message that says what to do:

| Configuration | Result |
|---|---|
| `PULP_ENABLE_AAX=ON`, no `PULP_AAX_SDK_DIR` | refused; names `PULP_AAX_SDK_DIR` |
| `PULP_AAX_SDK_DIR` is not an AAX SDK root | refused |
| `PULP_AAX_SDK_DIR` inside the source tree | refused |
| `SPECTR_AAX_HEADLESS=ON` without `PULP_ENABLE_AAX=ON` | refused |
| `PULP_ENABLE_AAX=ON` on Linux | refused |

`Spectr-aax-option` tests that decision on its own. `Spectr-aax-configure`
configures scratch projects against the build's real Pulp SDK package with no
AAX SDK present. The M5 gate runs both.

## Validating

Run Avid's DigiShell AAX Validator against the unsigned build before signing.
Pulp's `aax` skill lists the gates: describe, data model, parameters,
load/unload and a real Pro Tools session. Validator and host evidence belongs
in a dated receipt outside `Contents/MacOS`. `tools/relocate_aax_control_sidecars.cmake`
keeps build evidence out of the code directory, because `codesign` treats
anything there as nested code.

## Signing and packaging (owner steps)

Pro Tools loads only an AAX that PACE's `wraptool` has wrapped and signed.
That needs the owner's PACE account, iLok, wrap configuration and Developer ID
identity, so it runs on the owner's machine, never in CI.

1. Build `Spectr_AAX` from the exact release commit, and record the bundle's
   SHA-256.
2. Developer-ID sign the nested non-PACE code first. Then wrap and sign the
   outer bundle with `wraptool sign`. Never run `codesign` on the outer bundle
   after `wraptool`: that strips the wrap.
3. Check the result with `wraptool verify`, `codesign --verify --deep --strict`
   and a Pro Tools load.
4. Package it by running `package.sh` with `SPECTR_AAX_SIGNED` set to the
   signed `Spectr.aaxplugin`. The package step refuses a bundle that is
   unsigned or at a different version. Pulp's installer recipe then runs
   `wraptool verify` again before it adds a "Pro Tools" choice.

The release workflow pins `SPECTR_AAX_SIGNED` to an empty value, so a CI
release never carries an AAX. Shipping AAX in a release is a deliberate change
to that line, and the runner must also have the owner's PACE tooling.

## Windows

The CMake option also accepts Windows, as Pulp does, but Spectr's Windows AAX
is untested and nothing packages it. It needs an x64-only build lane (Pro Tools
for Windows loads x64 AAX), PACE signing with a Windows code-signing
certificate, an installer and release-check entry for
`C:\Program Files\Common Files\Avid\Audio\Plug-Ins`, and validation in Pro
Tools for Windows. It is tracked as a separate issue.
