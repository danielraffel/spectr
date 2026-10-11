# Windows capability matrix

This matrix records the tested local Windows environments and prevents
repeating a multi-gigabyte Ableton transfer when the guest CPU cannot satisfy
the installer prerequisite.

| Environment | CPU capability observed | Spectr proof | Ableton Live 12.4.6 |
| --- | --- | --- | --- |
| UTM 5.0.6 on M5/M5S, Windows 11 ARM64 | `OSXSAVE=false`, `AVX=false` | REAPER ARM64EC discovery, instantiation, UI screenshot, and 5-second render pass | Installer aborts before installation |
| Proxmox VM 300, Windows Server 2022 x64 | Intel Xeon E5-1650 v2; `AVX2=false` | Focused `Spectr processes audio` test passes | Installer aborts before installation |

The Ableton installer log is the authoritative prerequisite evidence:

- UTM: `.local-evidence/ableton/ableton-install.log`
- Proxmox: `.local-evidence/ableton/ableton-install-proxmox.log`

Both logs show the installer stopping during `Avx2Detection`, before files are
installed. A future Ableton attempt must use a newer AVX2-capable x64 Windows
host. The existing ARM UTM and 2013 Proxmox images remain useful for Spectr
builds and REAPER validation and should not be replaced for this reason.

## Preflight before transferring the trial package

On a candidate Windows host, run the installer with logging first:

```powershell
& '.\Ableton Live 12 Trial Installer.exe' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=C:\Users\Public\ableton-install.log
Select-String -Path C:\Users\Public\ableton-install.log -Pattern 'Avx2 Detected|InitializeSetup returned'
```

Only a log containing `Avx2 Detected = true` is a reason to transfer or extract
the full package for an interactive DAW test. This check does not alter Pulp
CI routing and does not consume a macOS runner slot.
