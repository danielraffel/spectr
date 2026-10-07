# Windows VM path

## Current durable layout

- Immutable golden: `/Volumes/Atelier/VMs/goldens/pulp-windows-build-24h2-arm64-2026-06-12-cacheopt.qcow2`.
- Single disposable candidate: `/Volumes/Atelier/VMs/bench/pulp-windows-build-24h2-arm64-utm-fresh-20261007.qcow2`.
- Both images pass `qemu-img check`. The failed 50 GB UTM import was removed after preserving the golden.
- The candidate is kept separate from macOS runners and is stopped when not in use.

## Boot evidence

The candidate boots on the M5 Ultra with the QEMU parameters below and accepts SSH on the forwarded port. The guest reported `Windows 10 Pro` and `ARM 64-bit Processor`.

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

The headless M5 ARM64 guest built Pulp `v0.915.0` and Spectr at commit
`2bef773265c0f3196db8fc77f761307cbbde9de6` with MSVC ARM64. Standalone, CLAP,
and VST3 linked, and `Spectr-test.exe "Spectr processes audio" --reporter
compact` passed with 2 assertions. The artifact receipt is in
`docs/windows-validation-arm64-2026-10-07.json`.

The next runtime gate is REAPER scan/load proof with logs and a screenshot;
Ableton follows only after REAPER passes.
