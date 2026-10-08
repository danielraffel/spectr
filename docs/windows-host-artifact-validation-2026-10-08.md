# Windows ARM64 host artifact validation — 2026-10-08

This receipt records the first headless Windows ARM64 host test of the built
Spectr VST3 artifact on the M5S QEMU guest. It is separate from REAPER and
does not claim a desktop DAW load.

## Environment

- Guest: Windows ARM64, native QEMU on the M5S host
- Source: `C:\spectr-gui`
- Build: `C:\builds\spectr-arm64-gpu`
- Artifact: `C:\builds\spectr-arm64-gpu\VST3\Spectr.dll`
- Host test: `C:\builds\spectr-arm64-gpu\Spectr-artifact-test.exe`
- Source revision in the guest: `2bef773265c0f3196db8fc77f761307cbbde9de6`

Windows emits the VST3 entry as a DLL, while macOS uses a bundle directory.
The CMake test definition now selects `VST3/Spectr.dll` on Windows so the
cross-platform host test loads the actual Windows artifact.

## VST3 result

Command, run through the bounded QEMU SSH wrapper:

```powershell
$env:PULP_DISABLE_PLUGIN_EDITOR = "1"
& C:\builds\spectr-arm64-gpu\Spectr-artifact-test.exe `
  "Pulp host loads and processes the built Spectr VST3 artifact" `
  --reporter compact
```

Output included:

```text
VST3: initialized 'Spectr' with 209 parameters
VST3 setBusArrangements: accepted 1 in / 1 out buses
All tests passed (1500 assertions in 1 test case)
```

This proves Windows ARM64 Pulp-host loading and audio processing of the VST3
artifact. It does not prove REAPER or Ableton compatibility.

## CLAP result

The corresponding CLAP test initialized the artifact and reached the same
audio assertions, but failed the final `slot.has_editor()` assertion because
the guest had no logged-in desktop session. It is therefore recorded as
headless runtime evidence with the editor gate open, not as a CLAP acceptance
pass.

## Remaining desktop gate

REAPER instantiation, runtime audio, screenshots, and Ableton validation still
require an authenticated Windows desktop session. The existing REAPER scan
receipt proves discovery only.
