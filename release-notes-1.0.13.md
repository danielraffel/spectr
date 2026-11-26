# Spectr 1.0.13

On Apple silicon Macs, Spectr's editor is now hosted by Vellum, the UI layer
that Pulp's audio plug-ins are moving to. The editor itself is unchanged: the
same panels, controls, and behavior, drawn through Vellum's GPU host. Audio
processing is identical to the previous editor host (a null test of the two
builds shows no difference). This release is built on the published Pulp SDK
1.211.0, the first Pulp release to carry Vellum 0.2.3.

Windows packages keep the previous native editor until Vellum ships on
Windows.

Choose the download for the operating system **inside the machine or VM where
you will run Spectr**. These artifacts are available for testing; publication
does not mean every format and platform has passed installed-product checks.

| Where you run Spectr | Download | What it is for |
| --- | --- | --- |
| macOS on an Apple silicon Mac (M-series) | `Spectr-1.0.13.pkg` | Native Mac installer with the Vellum-hosted editor (AU, VST3, CLAP and the standalone app). |
| Windows on an Intel or AMD PC | `Spectr-windows-x64-1.0.13.zip` | Windows x64 package; exact-archive host qualification remains incomplete. |
| Windows ARM in UTM on an Apple silicon Mac, or an ARM Windows PC | `Spectr-windows-arm64-1.0.13.zip` | Native ARM64 package for an ARM64 host such as ARM64 REAPER. It does not load into an x64 DAW. |

Windows packages are not code-signed. If SmartScreen warns, choose
**More info**, then **Run anyway**.

## Current qualification limits

- **Intel macOS and Linux:** no package in this release. Use 1.0.11 for those
  platforms.
- **Updates in Settings (macOS standalone):** with the Vellum-hosted editor the
  UPDATES group in Settings is not wired yet. **Spectr > Check for Updates…** in
  the app menu works as before, and automatic checks are unchanged.
- **Windows:** full qualification of these exact archives remains incomplete.
- **ARM64EC:** no ARM64EC package is included.
- GPU-audio Tracking behavior remains unproven and is not advertised as supported.
