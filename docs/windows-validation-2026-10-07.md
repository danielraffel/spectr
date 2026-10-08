# Windows validation receipt

This receipt records the first native x64 Windows proof from the Proxmox VM and
defines the repeatable local check. It is deliberately narrower than DAW
acceptance: artifact existence and CTest registration do not prove that a
plugin loads in REAPER or Ableton.

## Proxmox x64 evidence

- Guest: `pulp-win-ci` at `192.168.86.21`, Windows x64.
- Source checkout: `C:\spectr-win-proof` at commit
  `506cbf18e134d13400b6c4f4ed4072ca98aeab91`.
- Focused command:
  `Spectr-test.exe "Spectr processes audio" --reporter compact`
- Result: exit `0`; `All tests passed (2 assertions in 1 test case)`.
- CLAP: `C:\spectr-win-proof\build-win\CLAP\Spectr.clap`, 9,507,840 bytes,
  SHA-256
  `B995B009E983EEC7C55BF5B4C600EAE4E2ADAF4964507D0D235E613E53E65EDE`.
- VST3 binary: `C:\spectr-win-proof\build-win\VST3\Spectr.vst3\Contents\x86_64-win\Spectr.vst3`,
  SHA-256
  `329D2FBF55D1CB5B47B7FB6DD150E94308233B77A60635DCB776C125471C8620`.
- Standalone: `C:\spectr-win-proof\build-win\Spectr.exe`, SHA-256
  `675E9524ADE95BB85249F315A65E43B85AC356022365496CD4CCCBBC150C5013`.

The guest checkout contains local portability edits and diagnostic files. This
receipt therefore proves the observed build, not a clean upstream release.

After collecting this evidence, VM 300 was shut down cleanly. Its disk and
toolchain remain available for a scheduled nightly or an explicitly admitted
interactive run; leaving a 10 GB Windows guest running would prevent the Mac
Pro governor from admitting another 4-core Linux job.

## Repeatable check

From a Windows checkout, run:

```powershell
.\tools\windows\validate-build.ps1 -BuildDir C:\spectr-win-proof\build-win
```

The script fails closed when the focused test, standalone, CLAP, or VST3
binary is absent. It writes a JSON receipt with the exact artifact hashes.

## Still required

The ARM64 UTM lane must link against the MSVC-compatible Skia archive before
it can produce an ARM64 Spectr artifact. After that, REAPER must scan and load
the plugin with runtime logs and a screenshot; Ableton follows the REAPER pass.

## Headless ARM64 evidence

- Guest: TartCI/QEMU Windows 11 ARM64 overlay on the M5 Ultra, forwarded SSH port
  `50371`; the VM is disposable and does not consume a macOS runner slot.
- Pulp SDK: tag `v0.915.0`, source SHA
  `d9b218422382d29fafad7d116343b7b422eed44f`, installed with MSVC ARM64 and
  tests disabled for the SDK packaging build.
- Skia/Dawn: MSVC ARM64 archive from
  https://github.com/danielraffel/skia-builder/actions/runs/37685090727 with
  artifact SHA-256
  `c494cc3fc51b344b35ce776b77e6a70f1cb123b645f216aca07f7be4a281d9cc`.
- Spectr source: `2bef773265c0f3196db8fc77f761307cbbde9de6`.
- Focused command: `Spectr-test.exe "Spectr processes audio" --reporter compact`.
- Result: exit `0`; `All tests passed (2 assertions in 1 test case)`.
- Architecture: `ARM64`.
- CLAP: `C:\spectr-headless\build-arm64\CLAP\Spectr.clap`, 8,480,256 bytes,
  SHA-256 `46D7F14B335C4E83199D126DA41725C263D567A1F98362DDD50ABF6D379A5D5F`.
- VST3: `C:\spectr-headless\build-arm64\VST3\Spectr.dll`, 8,593,408 bytes,
  SHA-256 `3DB7C96D167A0A8AD4B889D69BB4F5DBB6E8F932CE4670957C1622FAD7AE5882`.
- Standalone: `C:\spectr-headless\build-arm64\Spectr.exe`, 9,154,560 bytes,
  SHA-256 `51DFC83853F04D631F30D98884FEBB2099AB2B9AD1E208D27A58C9C5638B2815`.

This is build and focused audio-test proof. It does not yet prove REAPER or
Ableton loading. The UTM desktop clone is being repaired separately from this
headless lane.

## Native ARM64 GPU/Skia proof (M5 Ultra QEMU guest)

The direct QEMU ARM64 guest is the productive headless path. UTM 5.0.6 still
returns to UEFI and does not provide a usable guest-agent/network state for this
image; this does not block the headless lane.

- Guest: Windows 11 ARM64, SSH `admin@127.0.0.1:50375`; disposable candidate
  `/Volumes/Atelier/VMs/bench/pulp-windows-build-24h2-arm64-utm-fresh-20261007.qcow2`.
- Pulp GPU build: MSVC 19.51.36260 / toolset 14.51.36231, `PULP_ENABLE_GPU=ON`,
  Skia/Dawn archive SHA-256
  `c494cc3fc51b344b35ce776b77e6a70f1cb123b645f216aca07f7be4a281d9cc`.
- Pulp CLI: `C:\builds\pulp-915-gpu-ninja18\tools\cli\pulp-cpp.exe`,
  27,180,032 bytes, SHA-256
  `151AC8F1BEBDD04DD8CE8A9F4757C90743A869C5AA937F2AFFCD19BA328DAA44`.
- SDK staging: `C:\pulp-gpu-sdk` completed with
  `cmake --install C:\builds\pulp-915-gpu-ninja18 --prefix C:\pulp-gpu-sdk --config Release`
  exit `0`; `PulpConfig.cmake` and 54 Pulp libraries are present.
- Spectr source: `2bef773265c0f3196db8fc77f761307cbbde9de6`.
- Spectr configure: `Pulp_DIR=C:\pulp-gpu-sdk\lib\cmake\Pulp`, Skia from the
  staged SDK; configure completed with MSVC ARM64.
- GPU build: `ninja Spectr_Standalone Spectr_VST3 Spectr_CLAP Spectr-test -j4`,
  186/186 steps succeeded.
- Focused test: `Spectr-test.exe "Spectr processes audio" --reporter compact`,
  exit `0`; 2 assertions in 1 test case.
- ARM64 artifacts:
  - `C:\builds\spectr-arm64-gpu\Spectr.exe`, 23,944,704 bytes, SHA-256
    `48faba70e4dda89653c8859a0a44470c551ed607f249bf0951eb667fc5f18999`.
  - `C:\builds\spectr-arm64-gpu\VST3\Spectr.dll`, 23,444,992 bytes, SHA-256
    `a0611bc25bb3f31af440c43e4ef82e684c88e9dff861e55d1ead19de9e1a92a3`.
  - `C:\builds\spectr-arm64-gpu\CLAP\Spectr.clap`, 23,331,328 bytes, SHA-256
    `59f7b1d6cc67e42230fed10fe1a9835c6d06807ad22a330ab3c19fca50498aac`.

### Negative host evidence

- `Spectr.exe` reaches `WindowHost::create()` with `PULP_AUDIO_DEVICE=null` but
  cannot open a GUI from the SSH-only QEMU session. Without the null device,
  Windows reports no WASAPI default output.
- `Spectr-artifact-test.exe` initialized the built CLAP (`CLAP: initialized
  'Spectr'`) but did not terminate in the headless session; it was stopped after
  the live process check.
- REAPER 7.82 x64 is installed at `C:\Program Files\REAPER (x64)\reaper.exe`,
  but the staged Spectr binary is pure ARM64. The x64 REAPER process therefore
  cannot provide ARM64 plugin ABI proof. A headless launch reached VST/CLAP scan
  initialization but did not terminate cleanly over SSH; it was stopped.
- Ableton is not installed, so there is no Ableton scan/load claim or screenshot.

The GPU/GUI build is therefore proven at compile and focused audio-test level.
REAPER scan/load remains the next acceptance gate, followed by Ableton.

## Repeatable ARM64 packaging and REAPER discovery

The productive M5 Ultra lane now has a reusable packaging check:

```powershell
.\tools\windows\package-vst3.ps1 -BuildDir C:\builds\spectr-arm64-gpu -Architecture arm64-win
.\tools\windows\reaper-scan.ps1 -Vst3Root 'C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win'
```

`package-vst3.ps1` creates the Windows VST3 layout
`Spectr.vst3\Contents\arm64-win\Spectr.vst3` and carries the ICU/WebGPU runtime
files beside the plugin. The live guest package was rebuilt and its plugin hash
is `A0611BC25BB3F31AF440C43E4EF82E684C88E9DFF861E55D1EAD19DE9E1A92A3`.

REAPER ARM64EC beta was installed from
https://www.reaper.fm/files/7.x/reaper782_win11_arm64ec_beta-install.exe.
The installer hash is
`CA562D2ABB6A8C7C9A3675CF70C4F1C3DA643338C9531DA5F0BF61B6EBEC36EB` and the
installed host hash is
`EA910AF76B1160411F54DEA978084D054BB8E397A391E7E84DE91088904C7F26`.
REAPER's cache contains `Spectr.dll=3B5E20C3E956DD01`, proving discovery by the
ARM64EC host. This is not yet load/audio proof; a desktop-capable session and a
screenshot are still required.

For a bounded interactive session on macOS, use
`tools/windows/launch-arm64-qemu.sh --display=cocoa`. It keeps one base image,
creates one linked overlay and firmware-vars copy, and removes both on exit.
Use `--display=none` for headless SSH work. This path is separate from TartCI
and does not consume a macOS runner slot.

## Toolchain provenance follow-up

The ARM64 Skia/Dawn producer now publishes `msvc-toolchain.json` provenance in
https://github.com/danielraffel/skia-builder/pull/29. The validated archive was
built with VS 18 / MSVC 14.51.36231, Windows SDK 10.0.26100.0, ARM64 COFF and
static `/MT`. Future Windows GPU rebuilds should retain that producer/consumer
match; the older MSVC 14.44 experiment failed in Dawn's ARM64 resource compiler
step and was not used for the accepted artifact.
