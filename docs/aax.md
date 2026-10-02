# AAX (Pro Tools) — local developer build

Spectr can build an AAX Native plug-in for Pro Tools. It is **off by default**
and changes nothing about the VST3 / AU / CLAP / Standalone builds or the
release installer.

## What it needs

- **Avid's AAX SDK**, downloaded by you from <https://developer.avid.com/aax/>
  (free Avid developer account, click-through license). It is never committed
  here and must live outside this repository, e.g.
  `~/SDKs/avid/aax-sdk/current` (that directory must contain
  `Interfaces/AAX.h`).
- **The same Pulp SDK the release uses.** No special Pulp build is required:
  the installed Pulp SDK already ships its AAX adapter sources
  (`src/pulp/format/aax_*.cpp`) and `PulpAAX.cmake`, and compiles Avid's
  `AAXLibrary` from your SDK at configure time. The release SDK pin stays as
  it is.

## Build

```bash
cmake -S . -B build-aax -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPulp_DIR=<pulp-sdk>/lib/cmake/Pulp \
  -DSPECTR_ENABLE_AAX=ON \
  -DPULP_AAX_SDK_DIR="$HOME/SDKs/avid/aax-sdk/current"
cmake --build build-aax --target Spectr_AAX -j8
# -> build-aax/AAX/Spectr.aaxplugin
```

`SPECTR_AAX_EDITOR` picks the UI Pro Tools shows:

| value | UI |
|---|---|
| `native` (default) | Spectr's own editor, via Pulp's AAX GUI shell |
| `strip` | Pro Tools' auto-generated parameter strip |

Pulp has not yet driven its AAX custom editor inside Pro Tools itself, so if
the editor misbehaves there, rebuild with `-DSPECTR_AAX_EDITOR=strip` to tell an
editor problem from an audio/model problem.

AAX identifiers: manufacturer `Pulp`, product `Spec`, plug-in type `SpeN`
(derived from the identity's AU code; dev and preview identities get their own).
Once an AAX build ships, these are as frozen as the AU subtype.

## Validate offline (no Pro Tools)

Avid's *DigiShell and AAX Validator* download (same portal) runs the AAX
validator suite headless:

```bash
cd ~/SDKs/avid/aax-validator/current/CommandLineTools
printf 'load_dish aaxval\nruntests "%s"\nquit\n' /abs/path/build-aax/AAX/Spectr.aaxplugin | ./dsh
```

Run one validator at a time; concurrent DigiShell sessions collide on local
ports and report `E_CANCELED` / `bind: Address already in use`. Remove the
download's quarantine attribute first (`xattr -dr com.apple.quarantine`).

## Running it in Pro Tools

The build is **unsigned**. Retail Pro Tools only loads AAX plug-ins signed with
PACE's Eden tools (it rejects unsigned ones, error -7054). For development use
the free **Pro Tools Developer** build: request a developer activation code
from Avid (`devauth@avid.com`), download it from your Avid account, and
activate it with iLok License Manager. To test, copy the bundle into
`/Library/Application Support/Avid/Audio/Plug-Ins/`.

Shipping to Pro Tools users additionally requires PACE signing (`wraptool`
from the Eden SDK, an iLok, and a PACE account Avid arranges for registered
developers) on every release, before Apple notarization. Spectr's
`package.sh` / Pulp's `build_combined_installer.sh` have no AAX path yet.
