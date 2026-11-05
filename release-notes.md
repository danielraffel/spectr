# Spectr 1.0.9

This release includes the latest macOS build and experimental Windows installers.

## Which download should I use?

| Environment | Download |
|---|---|
| macOS Apple Silicon | macOS package |
| Native Windows ARM64, including Windows ARM in UTM | `Spectr-Windows-ARM64-Setup.exe` |
| Windows ARM in UTM when testing x64 compatibility | ARM64EC installer |
| Windows ARM in UTM when deliberately testing Prism x64 emulation | `Spectr-Windows-Intel64-Setup.exe` |
| Intel or AMD Windows | `Spectr-Windows-Intel64-Setup.exe` |

The ARM64, ARM64EC, and Intel Windows packages are unsigned and experimental. They have been build, package, and installation checked in our qualification VMs. They have not been broadly tested on physical Windows systems or across all Windows DAWs and GPU configurations. UTM results do not guarantee native Windows behavior.

Windows users should rescan their VST3 and CLAP plugin paths in the DAW after installation. The standalone app is installed with the same package.
