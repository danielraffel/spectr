# Spectr 1.0.12

The release includes the current GPU renderer and Tracking integration, along with the native Windows release path. GPU-audio Tracking remains unproven and is not advertised as supported.

The ARM64EC package is omitted because the published Pulp SDK does not provide a matching ARM64EC/ARM64X provider; native ARM64 and x64 packages remain separate.

Fixes a crash when reopening a session that contains a saved Auto Gain estimate.
The restored estimate now remains valid while its spectrum rows are copied.
Linux builds include framework fixes for dropdown keyboard navigation, pointer
modifiers, popup focus, and hover cursors; interaction qualification is ongoing.

Choose the download for the operating system **inside the machine or VM where
you will run Spectr**. These artifacts are available for testing; publication
does not mean every format and platform has passed installed-product checks.

| Where you run Spectr | Download | What it is for |
| --- | --- | --- |
| macOS on an Apple silicon Mac (M-series) | [Spectr-1.0.12-macos-arm64.pkg](https://github.com/danielraffel/spectr/releases/download/v1.0.12/Spectr-1.0.12-macos-arm64.pkg) | Native Mac installer. This is not a Windows or Linux VM build. |
| macOS on an Intel Mac | [Spectr-1.0.12-macos-x86_64.pkg](https://github.com/danielraffel/spectr/releases/download/v1.0.12/Spectr-1.0.12-macos-x86_64.pkg) | Intel Mac installer. **Known issue: plugin editors fail to draw in REAPER on the Intel Mac mini; fix in progress.** |
| Windows on an Intel or AMD PC | [Spectr-windows-x64-1.0.12.zip](https://github.com/danielraffel/spectr/releases/download/v1.0.12/Spectr-windows-x64-1.0.12.zip) | Windows x64 package; exact-archive host qualification remains incomplete. |
| Windows ARM in UTM on an Apple silicon Mac, or an ARM Windows PC | [Spectr-windows-arm64-1.0.12.zip](https://github.com/danielraffel/spectr/releases/download/v1.0.12/Spectr-windows-arm64-1.0.12.zip) | Native ARM64 package for an ARM64 host such as ARM64 REAPER. It does not load into an x64 DAW. |
| ARM64 Linux in UTM/EZVM on an Apple silicon Mac, or another ARM64 Linux system | [Spectr-v1.0.12-linux-aarch64.tar.xz](https://github.com/danielraffel/spectr/releases/download/v1.0.12/Spectr-v1.0.12-linux-aarch64.tar.xz) | Native ARM64 Linux VST3, CLAP, and standalone. DAW/audio/interaction qualification remains incomplete. |
| Linux on an Intel or AMD PC | [Spectr-v1.0.12-linux-x86_64.tar.xz](https://github.com/danielraffel/spectr/releases/download/v1.0.12/Spectr-v1.0.12-linux-x86_64.tar.xz) | Linux x64 VST3, CLAP, and standalone. Separate x64 build; DAW/audio/interaction qualification remains incomplete. |

## Current qualification limits

- **Intel macOS:** the installed standalone rendered correctly. Exact installed
  VST3, CLAP, and AU binaries reproduce a Metal shader compilation failure in
  REAPER 7.86 on Intel UHD Graphics 630/macOS 15.8.3. A dependency fix is being
  reviewed; a repaired plugin installer has not yet been published. Earlier
  audio/state checks do not establish that these current plugin editors work.
- **Linux:** the reported menu, keyboard, modifier, hover, drawing, and repaint
  interactions still need a complete real-DAW regression run with audio.
- **Windows:** full qualification of these exact archives remains incomplete.
  A Windows x64 package may be tried under Windows ARM's x64 emulation with an
  x64 DAW, but that combination is not qualified here. The native ARM64 package
  uses a narrowly scoped MSVC STL compatibility shim required by the published
  v1.149.0 ARM64 SDK on the UTM toolchain.
- **ARM64EC:** no ARM64EC package is included; the current published ARM64 SDK
  cannot be linked as ARM64EC with the UTM toolchain. Native ARM64 and x64 are
  separate packages, not a unified ARM64EC installer.
- GPU-audio Tracking behavior remains unproven and is not advertised as supported.
