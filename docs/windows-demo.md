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

## Cleanup

Stop the launcher with `Ctrl-C`. The linked QCOW2 overlay and copied firmware
variables are removed by the launcher trap. The 75 GB base image is retained;
no second golden image is created.
