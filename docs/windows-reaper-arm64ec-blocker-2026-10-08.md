# Windows REAPER ARM64EC compatibility blocker — 2026-10-08

The M5S Windows guest and the Spectr ARM64 build are healthy, but the current
REAPER ARM64EC host does not accept the native ARM64 VST3 as a usable plug-in.
This is a real host compatibility failure, not a cache-only problem.

## Evidence

- REAPER ARM64 is an ARM64X hybrid executable (`dumpbin /headers` reports
  `8664 machine (x64) (ARM64X)` and an ARM64EC build path).
- Spectr is a native ARM64 DLL (`AA64 machine (ARM64)`).
- Windows `LoadLibrary` plus `GetPluginFactory` succeeds for Spectr, and the
  factory returns one class (`Audio Module Class`, `Spectr`).
- Pulp's Windows artifact host loads the same DLL and passes 1,500 assertions.
- REAPER's cache records `Spectr.vst3=3B5E20C3E956DD01`, but the Add FX browser
  contains no Spectr entry after a full cache clear and rescan.
- REAPER's failed-scan list contains both:

  ```text
  C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win\Spectr.vst3
  C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win\wgpu_native.dll
  ```

- No Spectr application-crash event is emitted. The failure is therefore at
  REAPER's scanner/ABI boundary rather than a Windows process crash.

## Attempted ARM64EC build

Adding `/arm64EC` to the Spectr build produces ARM64EC objects, but linking
fails against the existing ARM64 Pulp SDK with unresolved `EC Symbol`
references. The SDK and its static libraries must be produced for ARM64EC (or
ARM64X) before an ARM64EC Spectr plug-in can be built.

This is why copying or renaming the existing ARM64 DLL is not a fix. The
current package remains valid for native ARM64 hosts and for the Pulp Windows
artifact host, but it is not accepted by this REAPER ARM64EC build.

## Required next packet

1. Produce an ARM64EC/ARM64X Pulp SDK, including its static libraries and
   Skia/WebGPU dependencies.
2. Build Spectr's VST3 against that SDK and verify the PE machine type.
3. Install only that candidate into the disposable guest, clear REAPER's VST
   cache, and require `Spectr` to appear in Add FX.
4. Instantiate it, capture a desktop screenshot, and render a short audio
   receipt before attempting Ableton.

Until those steps pass, the Windows DAW status is **blocked at REAPER
instantiation**. The native ARM64 compile, package, and headless Pulp host
proof remain green and are recorded separately.
