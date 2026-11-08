# Spectr 1.0.9

This release includes the macOS package and three experimental Windows
packages. Use the row that matches the operating system and processor of the
machine running your DAW or standalone app.

## Which download should I use?

| Your machine | Download | What it is for |
|---|---|---|
| Mac with Apple silicon (M-series) | `Spectr-1.0.9.pkg` | The signed and notarized macOS installer. Installs the standalone app and the AU, VST3, and CLAP plug-ins. |
| Windows on ARM64, running ARM64 software natively | `Spectr-windows-arm64-1.0.9.zip` | Native ARM64 Windows. This is the normal choice for an ARM Windows computer or an ARM Windows VM when the DAW is also ARM64. |
| Windows on ARM64, with an ARM64EC/ARM64X-capable host | `Spectr-windows-arm64ec-1.0.9.zip` | The compatibility build for ARM Windows hosts such as an ARM64EC-capable REAPER. Use this when the host expects ARM64EC plug-ins. |
| Windows on Intel or AMD (x64) | `Spectr-windows-x64-1.0.9.zip` | The native Intel/AMD Windows build. This is the one to send to someone using a conventional x64 Windows PC. |
| Windows on ARM64 running an x64-only DAW through Prism | `Spectr-windows-x64-1.0.9.zip` | Optional compatibility testing under Windows' x64 emulation. Prefer the ARM64 or ARM64EC row when the DAW supports ARM64. |

The Windows ZIPs contain the installer and the standalone application. They
are unsigned experimental builds, so Windows SmartScreen may require choosing
**More info** and **Run anyway**. After installation, rescan the VST3 and CLAP
paths in the DAW if the plug-ins do not appear immediately.

Linux packages are not included in 1.0.9 yet. When that lane is qualified, it
will use separate `linux-aarch64` and `linux-x86_64` archives; neither should
be substituted for a Windows package.

Windows qualification was performed in our retained VM environments and is
not broad coverage of physical Windows systems, DAWs, or GPU configurations.
An ARM VM result does not guarantee identical behavior on native Windows
hardware.
