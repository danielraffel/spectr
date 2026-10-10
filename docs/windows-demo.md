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

## Optional UTM interactive path

For the repeatable interactive path, run this one host-side command from the
checkout:

```bash
tools/windows/open-utm-desktop.sh
```

It starts the retained VM if needed, waits for SSH, repairs RDP, and opens Jump
Desktop. The saved `pulp-win-ci` connection now matches the guest hostname
`PULP-WIN-CI` and local `ci` account, so there is no per-run domain mismatch.
The guest desktop has a `REAPER (ARM64)` shortcut pointing at the installed
ARM64 binary. Ableton Live is not installed in this ARM64 guest: its current
installer rejects the virtual CPU because AVX/AVX2 are unavailable. Use the
Windows x64 Proxmox session for Ableton Live validation.

UTM 5.0.6 is registered as the persistent interactive Windows VM. Its network
mode is **Emulated** with two forwarding rules:

```text
TCP 127.0.0.1:50376 -> 10.0.2.15:22
TCP 127.0.0.1:53390 -> 10.0.2.15:3389
```

Start it with:

```bash
/Applications/UTM.app/Contents/MacOS/utmctl start \
  36132D8D-99F7-4916-A6D8-935A6F4AF55F
SPECTR_WINDOWS_SSH_PORT=50376 \
  tools/windows/ssh-qemu-health.sh
```

If `utmctl list` reports `started` but the forwarded port does not answer,
open the VM window in UTM and press **Start/Resume** once. UTM 5.0.6 can leave
the QEMU process paused after a CLI start; the health guard will then wait and
fail closed instead of treating the listener as a booted guest.

The interactive image was repaired by replacing its undersized EFI variable
store with the known-good 64 MiB template, then restarting UTM. Current
verification is live: five SSH probes pass, RDP authentication succeeds, and
Jump Desktop reaches the Windows desktop at `127.0.0.1:53390`. The account is
`pulp-win\\admin`; its generated password is kept only in the local `0600`
file `/Users/danielraffel/.config/pulp/secrets/spectr-windows-utm-admin`.
1Password CLI is not connected on this Mac, so no vault backup was created.

The warmed Spectr build cache and disposable linked clones remain on the direct
QEMU golden image. UTM uses one retained interactive image and does not consume
a TartCI runner or create another golden image.

## Enable RDP for this overlay

### M5/M5S display diagnosis

The ARM64 Windows base currently boots and serves SSH, but its local QEMU
framebuffer is not a usable desktop. On 2026-10-08, `ramfb`, `virtio-gpu-pci`,
`bochs-display`, and VGA all produced an inactive framebuffer; the guest
reported `Microsoft Basic Display Adapter` with `ConfigManagerErrorCode=10`.
This is a Windows ARM64 guest display-driver/device mismatch. It is not a
build failure and does not justify cloning or rebuilding the 75 GB base image.

RDP is the supported interactive transport for this image. Jump Desktop now
connects to `127.0.0.1:53390`; plugin UI and audio acceptance are recorded in
`docs/windows-receipts-2026-10-10/utm-health-receipt.json`.

The base image keeps desktop access off. Repair the interactive desktop through
the authenticated SSH path before opening Jump Desktop:

```bash
SPECTR_WINDOWS_SSH_PORT=50375 \
  tools/windows/ensure-rdp-over-ssh.sh
```

The helper repairs the RDP registry flag, firewall group, and dependent
services on every disposable overlay and fails closed unless TCP 3389 is
listening. It never creates or stores a Windows password. RDP still requires
the Administrator password in Jump Desktop; SSH key authentication is not
reused as a desktop credential. The lower-level
`tools/windows/enable-rdp.ps1` remains available for a manually authenticated
PowerShell session.

Fresh-clone verification on 2026-10-09 returned
`fDenyTSConnections=0`, `term_service=Running`, and `rdp_listener=true`.

Add a Jump Desktop RDP connection to `127.0.0.1:53390`, accept the local
self-signed certificate when prompted, and enter `pulp-win\\admin` plus the
password from the local credential file.

## Verify REAPER discovery

The ARM64 host and packaged plugin are already part of the warm base. After
login, run:

```powershell
.\tools\windows\reaper-scan.ps1 `
  -Architecture arm64-win `
  -Vst3Root 'C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win'
```

The resulting receipt proves discovery and hashes the exact host and plugin.
It is explicitly **discovery-only**: a cache entry can exist while REAPER
lists the plug-in under “Plug-ins that failed to scan”. The helper records
`acceptance_status=blocked` unless a matching observed host-instance receipt
is supplied. It never treats a cache entry as proof of loading, instantiation,
audio, or a screenshot.

When a desktop observation is available, run the strict form and provide the
exported failed-scan evidence plus the host receipt:

```powershell
.\tools\windows\reaper-scan.ps1 `
  -Architecture arm64-win `
  -Vst3Root 'C:\Program Files\Common Files\VST3\Spectr.vst3\Contents\arm64-win' `
  -FailedScanEvidence C:\Users\admin\reaper-failed-scan.txt `
  -ObservedAcceptanceReceipt C:\Users\admin\reaper-acceptance-receipt.json `
  -RequireAcceptance
```

The strict form fails if the failed-scan evidence names Spectr, if the receipt
does not set both `plugin_instance_observed=true` and
`failed_scan_list_empty=true`, or if its REAPER/plugin hashes do not match the
current files.

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
artifacts, runtime DLL/data files when present, a SHA-256 manifest, and
`Install-Spectr.ps1`. The installer verifies every manifest hash before it
writes the VST3, CLAP, and standalone files to their standard Windows
locations. The pre-installer package from the earlier fresh clone was
35,675,271 bytes with SHA-256
`AA9314A2A5E3AF1C205CAD40E9E117F7107937303BF42D60A0243C7294614DAE`.

Install that package into the standard Windows locations from an elevated
PowerShell prompt:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\windows\install-package.ps1 `
  -Package C:\Users\admin\Spectr-windows-arm64-win.zip
```

The installer verifies every manifest hash before copying the standalone,
VST3, and CLAP artifacts. It is a development installer script, not a signed
MSI. The current REAPER acceptance is stronger than discovery-only: the live
UTM session showed `CLAP: Spectr (Pulp)` in the FX browser, instantiated it on
a track, and rendered a non-silent five-second WAV. Exact hashes and render
statistics are in `docs/windows-receipts-2026-10-10/utm-health-receipt.json`.
Ableton remains a separate installation and acceptance step.

## Ableton trial check on the ARM64 UTM guest

The official Ableton Live 12.4.6 Windows trial package was copied to the UTM
guest and verified before launch:

```text
SHA-256: 172e5c6da112cc6ac28b042435cc922d57a38df87dcd2dc4dcdec1bcf006f94e
```

The installer reached its prerequisite check but stopped before installation.
Its log reports `Windows architecture: Arm64`, then `Avx2 Detected = false`
and `OSXSAVE: false, AVX: false`; Ableton rejected the virtual CPU as below
the minimum requirement. The preserved log is
`.local-evidence/ableton/ableton-install.log` (SHA-256
`c5910ecdcf2a84645d3c3ed7c710e3a03116fc9e8a981b022beb8d2d5d52355a`).

This is an ARM UTM CPU capability limitation. It does not invalidate the
working REAPER/Spectr proof. The x64 Proxmox guest was checked as the next
candidate and also failed the same prerequisite: its 2013 Xeon E5-1650 v2
reports `Avx2 Detected = false` (it has AVX but not AVX2). That log is
`.local-evidence/ableton/ableton-install-proxmox.log` (SHA-256
`ea704344e28b3ee60b2cc6db080755f82bd21b34f335f13363d6a4f6b728b683`). Ableton
validation therefore requires a newer AVX2-capable x64 Windows host; no
Ableton success is claimed for either current VM.

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

## Proxmox Ableton launch evidence (2026-10-10)

The older official Ableton Live 10.1.43 trial is installed on VM 300 and was
launched in the authenticated `ci` desktop session. The screenshot is
[`.local-evidence/ableton/proxmox-ableton-live10-desktop.png`](../.local-evidence/ableton/proxmox-ableton-live10-desktop.png)
(SHA-256 `70006c16d06195c3fb75ab5af0c35adc29f507ea1138881b0c05b8eaee86efaa`).
The exact receipt is
[`docs/windows-receipts-2026-10-10/proxmox-ableton-live10-receipt.json`](windows-receipts-2026-10-10/proxmox-ableton-live10-receipt.json).

This proves the desktop application launches. It does not prove Spectr loads
in Ableton: the Live 10 trial welcome dialog remained open and no plugin or
audio acceptance was recorded. Live 12 remains blocked by the AVX/AVX2
preflight on both current guests; a newer AVX2-capable x64 Windows host is
required for current Ableton validation.

## Proxmox direct RDP path

The Proxmox x64 guest is reached directly at `192.168.86.21:3389`; the local
forward `127.0.0.1:53390` belongs to the ARM64 UTM guest and must not be used
for Proxmox. The interactive account is stored locally in
`/Users/danielraffel/.config/pulp/secrets/spectr-windows-proxmox-rdp` with mode
`0600`. A direct SDL FreeRDP session was authenticated on 2026-10-10. This
keeps interactive validation independent of the UTM port and does not reserve a
macOS CI runner.

## Proxmox x64 REAPER evidence (2026-10-10)

The direct Proxmox session at `192.168.86.21:3389` now has a second host proof:
REAPER 7.82 scanned `Spectr.vst3`, the FX browser listed `VST3: Spectr (Pulp)`,
and the plug-in was added to Track 1. The screenshot is
[`.local-evidence/proxmox/reaper-x64-spectr-instantiated.png`](../.local-evidence/proxmox/reaper-x64-spectr-instantiated.png)
(SHA-256 `58fda441c4586f4bb126ca5da86b05b7e623310ed909881e405c8aebccf4635a`).
The exact receipt is
[`docs/windows-receipts-2026-10-10/proxmox-reaper-x64-receipt.json`](windows-receipts-2026-10-10/proxmox-reaper-x64-receipt.json).

This is x64 scan and instantiation proof. The editor surface remained blank
white on this guest and REAPER had no selected audio device, so this receipt
does not claim x64 UI rendering or audio acceptance. The UTM ARM64 receipt
remains the stronger UI and audio proof.
