# Windows VM path

## Current durable layout

- Immutable golden: `/Volumes/Atelier/VMs/goldens/pulp-windows-build-24h2-arm64-2026-06-12-cacheopt.qcow2`.
- Single disposable candidate: `/Volumes/Atelier/VMs/bench/pulp-windows-build-24h2-arm64-utm-fresh-20261007.qcow2`.
- Both images pass `qemu-img check`. Stale failed-import images, their firmware
  vars, an old overlay, and a duplicate bench copy were moved to Trash; the
  bench directory now contains only the 75 GB candidate and its firmware.
- The candidate is kept separate from macOS runners and is stopped when not in use.

## Boot evidence

The candidate boots on the M5 Ultra with the QEMU parameters below and accepts SSH on the forwarded port. The guest reported `Windows 10 Pro` and `ARM 64-bit Processor`.

The SSH wrapper uses an explicit identity, no inherited stdin, bounded connection attempts, and keepalives. The guest retains Windows OpenSSH's default `MaxStartups 10:30:100`; automation therefore uses sequential probes and a single build session rather than opening a burst of SSH connections. A ten-session burst can legitimately receive `Connection reset by peer` from that guest limit even while the VM is healthy.

```text
qemu-system-aarch64 -accel hvf -machine virt,highmem=on,gic-version=3
  -cpu host -smp 8 -m 8192
  -drive if=pflash,format=raw,readonly=on,file=edk2-aarch64-code.fd
  -drive if=pflash,format=raw,file=efivars.fd
  -device ramfb -device nvme ...
```

This proves the Windows disk and native ARM virtualization path are healthy.

## UTM result

UTM 5.0.6 imports the same candidate but does not reach a usable guest-agent/network state. Its QEMU wrapper adds UTM-specific devices and firmware handling; `utmctl ip-address` reports that the guest agent is not running, and the guest repeatedly returns to UEFI. The UTM package is stopped and not used for build claims.

The practical interactive path is therefore direct QEMU with the known-good parameters until the UTM configuration is corrected. This does not block the headless ARM64 build lane.

## Build evidence

## Evidence refresh: 2026-10-22

The current local package lane has been rechecked against the exact Spectr
source `e992cdc7ea173bc555d4d93f5a8af19d7116e08b`.

| Lane | Current evidence | Disposition |
| --- | --- | --- |
| M5/M5S direct QEMU ARM64 | Release SDK `0.998.0`, native `arm64-win` VST3/CLAP/standalone package; package SHA-256 `4135aa9c37c6a03de5dc7ff775c099d30a561e1910d7f5879e56b0ba8890c855`; install receipt matched the package manifest | Build/package/install PASS; DAW UI acceptance pending |
| Proxmox VM 300 x64 | `C:\spectr-build-current-x64\Spectr-test.exe "Spectr processes audio" --reporter compact`; `All tests passed (2 assertions in 1 test case)` | Focused audio PASS; VM stopped after validation |

The ARM package is native ARM64 (`arm64-win`). The current Skia release has
ARM64 and x64 slices but no ARM64EC slice, so it cannot yet be accepted as a
GPU-editor payload for an ARM64X REAPER host. A REAPER cache entry or package
installation alone is discovery evidence; current host-instance, UI, audio,
and Perfetto acceptance remains unproven until an ARM64EC-compatible provider
is available.

The ARM launcher now makes a writable copy of read-only golden EFI vars before
booting. This keeps the golden immutable and makes linked overlays repeatable.

The headless M5 ARM64 guest built Pulp `v0.915.0` and Spectr at commit
`2bef773265c0f3196db8fc77f761307cbbde9de6` with MSVC ARM64. Standalone, CLAP,
and VST3 linked, and `Spectr-test.exe "Spectr processes audio" --reporter
compact` passed with 2 assertions. The artifact receipt is in
`docs/windows-validation-arm64-2026-10-07.json`.

The next runtime gate is REAPER scan/load proof with logs and a screenshot;
Ableton follows only after REAPER passes.
