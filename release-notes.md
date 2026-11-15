# Spectr 1.0.10

This release ships one matching macOS package, three experimental Windows
packages, and two Linux archives built from the same exact Spectr source
revision. Choose the row for the computer running your DAW or standalone app.

## Which download should I use?

| Your computer | Download | Use this when |
|---|---|---|
| Mac with Apple silicon | `Spectr-1.0.10.pkg` | You are running Spectr directly on macOS. The package installs the standalone app and AU, VST3, and CLAP plug-ins. |
| Windows ARM64, native ARM64 software | `Spectr-windows-arm64-1.0.10.zip` | Windows and the DAW are running ARM64 natively. |
| Windows ARM64 with an ARM64EC/ARM64X host | `Spectr-windows-arm64ec-1.0.10.zip` | The DAW is an ARM64EC/ARM64X host. For an x64-only DAW under Prism, use the x64 package below. |
| Intel or AMD Windows (x64) | `Spectr-windows-x64-1.0.10.zip` | You are using a conventional Intel or AMD Windows computer. |
| Windows ARM64 running an x64-only DAW through Prism | `Spectr-windows-x64-1.0.10.zip` | You are deliberately testing the x64 emulation path; prefer ARM64 or ARM64EC when the host supports it. |
| Linux on ARM64/AArch64 | `Spectr-v1.0.10-linux-aarch64.tar.xz` | Native ARM64 Linux, including an AArch64 Omarchy guest in UTM on Apple silicon. |
| Linux on Intel or AMD x86_64 | `Spectr-v1.0.10-linux-x86_64.tar.xz` | Native x86_64 Linux on Intel/AMD hardware or an x86_64 Linux VM. |

The Windows archives contain the unsigned PowerShell installer/launchers,
standalone app, VST3, CLAP, and required runtime files. Windows SmartScreen
may require **More info** and **Run anyway**. Rescan VST3 and CLAP paths in the
DAW after install.

The ARM64 Linux qualification covers native REAPER VST3 and CLAP loading,
editor/GPU initialization, VST3 automation, and non-silent renders. The
REAPER/Linux CLAP parameter bridge did not provide readback, so CLAP automation
is not claimed for that lane. The x86_64 archive has native ELF binaries and
installer smoke evidence; its qualification VM had no ALSA default device or
DAW/scanner, so test it in a real audio host before relying on x86_64 runtime
audio.

Windows support is experimental across physical hardware, DAWs, and GPU
configurations. The ARM64EC row is the recommended choice for Windows ARM in
UTM when the DAW is ARM64EC-capable.
