# Windows ARM64 demo workflow on macOS

This workflow uses one immutable ARM64 base image and one disposable linked
overlay. It runs a direct QEMU process on the Mac and never reserves a TartCI
runner.

## Start a disposable desktop

From this checkout:

```bash
SPECTR_WINDOWS_SSH_PORT=50375 \
SPECTR_WINDOWS_RDP_PORT=53389 \
tools/windows/launch-arm64-qemu.sh --display=cocoa
```

In a second terminal, wait for the guest SSH service:

```bash
SPECTR_WINDOWS_SSH_PORT=50375 tools/windows/ssh-qemu-health.sh
```

The guard absorbs normal Windows boot resets for up to 120 seconds, then runs
five bounded probes. It fails closed when the guest, key, or port is wrong.

## Enable RDP for this overlay

The base image keeps desktop access off. Copy and run the setup script as the
Windows Administrator through SSH:

```bash
scp -P 50375 tools/windows/enable-rdp.ps1 \
  admin@127.0.0.1:C:/Users/admin/enable-rdp.ps1
tools/windows/ssh-qemu.sh \
  'powershell -ExecutionPolicy Bypass -File C:/Users/admin/enable-rdp.ps1'
```

The script enables the listener and firewall only. It never creates or stores a
Windows password. RDP requires the user to enter the Administrator password in
Jump Desktop; SSH key authentication is not reused as a desktop credential.

Fresh-clone verification on 2026-10-09 returned
`fDenyTSConnections=0`, `term_service=Running`, and `rdp_listener=true`.

Add a Jump Desktop RDP connection to `127.0.0.1:53389`, accept the local
self-signed certificate when prompted, and enter the Windows credentials.

## Verify REAPER discovery

The ARM64 host and packaged plugin are already part of the warm base. After
login, run:

```powershell
.\tools\windows\reaper-scan.ps1 `
  -Architecture arm64-win `
  -Vst3Root 'C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win'
```

The resulting receipt proves discovery and hashes the exact host and plugin.
It does not claim plugin instantiation or audio until those are observed in the
desktop host.

The scan helper reads the cache with a bounded raw read and regex so it remains
reliable through Windows OpenSSH. Fresh-clone verification returned:

- REAPER ARM64 SHA-256: `95B3D2B226519F305A6C575105EBF8A11625A8C2EBA4F306DC47FB6B7774CDB8`
- Spectr VST3 SHA-256: `A0611BC25BB3F31AF440C43E4EF82E684C88E9DFF861E55D1EAD19DE9E1A92A3`
- Cache entry: `Spectr.dll=3B5E20C3E956DD01`

## Launch the desktop demo

After logging into Windows, use the fail-closed helper to verify the active
desktop session, hash the exact ARM64 host and plugin, and launch REAPER:

```powershell
.\tools\windows\reaper-demo.ps1 `
  -Architecture arm64-win `
  -Vst3Root 'C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win' `
  -Launch
```

Without an active Windows desktop session the helper exits with an actionable
error. Its receipt proves the launch inputs and process, while plugin
instantiation, audio, and screenshots still require an observed DAW receipt.

## Build a portable handoff

From the Windows guest, package the warmed build tree:

```powershell
.\tools\windows\package-release.ps1 `
  -BuildDir C:\builds\spectr-arm64-gpu `
  -Architecture arm64-win `
  -Output C:\Users\admin\Spectr-windows-arm64.zip
```

The package contains `Standalone/Spectr.exe`, the ARM64 VST3 and CLAP
artifacts, runtime DLL/data files when present, and a SHA-256 manifest. A fresh
clone produced a 35,675,271-byte ZIP with SHA-256
`AA9314A2A5E3AF1C205CAD40E9E117F7107937303BF42D60A0243C7294614DAE`.

The lower-level `package-vst3.ps1` helper also derives its destination and
receipt paths after binding `BuildDir`; its default invocation now succeeds on
the warmed ARM64 build tree.

For a complete artifact and focused-test receipt, run
`tools/windows/validate-build.ps1 -BuildDir C:\builds\spectr-arm64-gpu
-Architecture arm64-win`. Architecture selection is explicit, and the optional
Git SHA lookup is non-fatal when the build tree is not a checkout. A fresh
clone returned `PASS` with the two-assertion `Spectr processes audio` test.

## Bounded Proxmox x64 check

The Intel path can run without RDP through the Proxmox guest agent:

```bash
tools/windows/proxmox-validate.sh
```

It starts VM 300 only when stopped, waits for the guest agent, runs the exact
`Spectr processes audio` test, and stops the VM in its exit trap only when this
invocation started it. A live run passed with exit code `0` and two assertions;
the VM was confirmed `stopped` afterward.

## Cleanup

Stop the launcher with `Ctrl-C`. The linked QCOW2 overlay and copied firmware
variables are removed by the launcher trap. The 75 GB base image is retained;
no second golden image is created.
