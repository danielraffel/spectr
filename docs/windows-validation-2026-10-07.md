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
