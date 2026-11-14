# Spectr 1.0.10

This release ships one matching macOS package and three experimental Windows
packages built from the same exact Spectr source revision. Choose the row for
the computer running your DAW or standalone app.

## Which download should I use?

| Your computer | Download | Use this when |
|---|---|---|
| Mac with Apple silicon | `Spectr-1.0.10.pkg` | You are running Spectr directly on macOS. The package installs the standalone app and AU, VST3, and CLAP plug-ins. |
| Windows ARM64, native ARM64 software | `Spectr-windows-arm64-1.0.10.zip` | Windows and the DAW are running ARM64 natively. |
| Windows ARM64 with an ARM64EC/ARM64X host | `Spectr-windows-arm64ec-1.0.10.zip` | The DAW is an ARM64EC/ARM64X host, including the Prism compatibility path in an ARM Windows UTM VM. |
| Intel or AMD Windows (x64) | `Spectr-windows-x64-1.0.10.zip` | You are using a conventional Intel or AMD Windows computer. |
| Windows ARM64 running an x64-only DAW through Prism | `Spectr-windows-x64-1.0.10.zip` | You are deliberately testing the x64 emulation path; prefer ARM64 or ARM64EC when the host supports it. |

The Windows archives contain the unsigned installer, standalone app, VST3,
CLAP, and required runtime files. Windows SmartScreen may require **More
info** and **Run anyway**. Rescan VST3 and CLAP paths in the DAW after install.

Linux archives are not part of 1.0.10. A future Linux release will publish
separate `linux-aarch64` and `linux-x86_64` archives only after both lanes are
independently built and qualified.

Windows support is experimental across physical hardware, DAWs, and GPU
configurations. The ARM64EC row is the recommended choice for Windows ARM in
UTM when the DAW is ARM64EC-capable.
