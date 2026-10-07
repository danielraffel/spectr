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
