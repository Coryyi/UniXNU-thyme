> Historical bring-up notes: the dated observations below describe earlier stages.
> Current status and build limitations are documented in the repository root README.

# umi UEFI menu bring-up overlay

This package is a DEBUG firmware candidate for Xiaomi Mi 10 / SM8250.
Temporary boot attempts 02 and 03 reached SEC, MMU setup, PEI and DXE on the
handset, confirmed by recovered ramoops logs. Attempt 03 entered DisplayDxe
without returning. Attempt 04 traced its initialization into HALIOMMU's attach
path; all 129 logged service calls returned, but DisplayDxe did not. Attempt 05
omits HALIOMMU as a controlled experiment: the unchanged DisplayDxe has a
nonfatal missing-protocol path, while Kona SEC preserves existing MDP SIDs.
Attempt 05 then identified the J2 MP panel at 1080x2340 and returned Success
from DisplayDxe, but DXE stopped before BDS because Capsule Arch was absent.
Attempt 06 adds source CapsuleRuntimeDxe with its null backend and all update
paths disabled. It reaches BDS and the UniXNU menu loop, publishing a
1080x2340 GOP at 0x9c000000 with pitch 1080 and BGR reserved 8-bit format.
The user still sees no menu; the log reports DSI writes and brightness commands
failing. Attempt 07's read-only MDP SMMU snapshots before/after platform
initialization and after the first menu draw are identical; the physical
screen is still black. Static analysis identifies status 0xD as a DSI
completion polling timeout. Attempt 08 records eight successful GOP Blts and
approximately correct 100-us stalls, but no sampled DSI DMA completion bit.
The physical display remained black through attempt 08. Attempt 09's source-only
inherited-framebuffer profile displayed the complete UniXNU menu, confirmed by
the user. The recovered 11,145-byte console records the GOP, eight successful
Blts and three menu entries. This baseline has no input or XNU payload.
Attempts 10–12 verify volume-key selection and power-key confirmation events.
Attempt 13 additionally verifies short-power warm Restart back into Android;
touch remains nonfunctional.
Attempt 15 completes the UEFI checklist milestone: menu, framebuffer, and a
read-only RAM FAT payload volume. The menu loads its configuration and an
external EFI from that volume; the EFI verifies file contents, EOF and a seek,
displays a green success screen, and automatically warm-reboots into Android.
The user observation and recovered log agree. The pinned baseline is
`out/umi/uefi-step1-baseline.json`.
This is not a completed SMMU or display port. See `docs/umi-uefi.md` in the
workspace for per-attempt evidence.

## Display profiles

`tools/build_umi_uefi.py --display-profile vendor` retains the original
24-module selection. `--display-profile inherited` removes all vendor
executables and resources from the generated selection, and adds
`Drivers/InheritedFbDxe`. This GOP writes only the reserved framebuffer using
FrameBufferBltLib. It does not initialize DSI, clock or PMIC hardware and does
not trigger command-panel refresh. The layout comes from the GOP observed
after vendor initialization in attempts 06-08, and the complete menu was
physically visible with it in attempt 09. Repeated-boot stability, refresh
control and suspend/resume are not established. The original overlay and imported binaries remain
available for the vendor profile. Both profiles retain the same core services,
SEC setup and menu logging.

The verified image and exact build inputs are archived in
`out/umi/boot/attempt-09/`; `out/umi/menu-baseline.json` pins the image and runtime
evidence. Specify `--display-profile inherited` to reproduce this route;
the compatibility default remains `vendor`.

## Input profiles

`--input-profile buttons|touch` requires the inherited display profile and adds
the source button driver, a private menu loop, and the umi reset library.
The default `none` preserves the display baseline. Buttons use read-only SPMI
observer transactions. Touch uses bounded GPIO I2C and guarded power setup;
see `Drivers/UmiTouchDxe/SOURCES.txt` for its current hardware contract.
Input failures must not be interpreted as valid tap-up events.

Attempt 12 found PM8150A LDO1 disabled at its existing 1800-mV setting, with no
EE0 SPMI write channel. Touch power therefore needs an RPMh resource request;
SPMI ownership must remain unchanged. The same attempt found PSCI replacement
reset blocked by unavailable SCM dload function 02000110. Fixed device kernel
source supplies a secure IO-write fallback. The reset fallback passed warm Restart testing in attempt 13; touch power
still needs hardware verification. Short power confirmation and the separate long-power recovery
gesture are recorded separately.

Attempt 13 implements a guarded `ldoc1` enable request through the APPS DRV2
active TCS, followed by read-only physical LDO checks and the touch power/reset
sequence. It does not change SPMI ownership or request voltage/mode changes.
Only completed, idle TCS slots are restored; an in-flight timeout retains
state for bounded cleanup retries. Releasing this request does not establish
the unknown pre-handoff per-DRV vote. SPMI writes are now restricted to the
three primary PON registers.

The reset library also implements the fixed kernel's SCM secure IO-write
fallback, gated by service availability. The attempt 13 image passed the host
checks and was temporarily booted. The user confirmed automatic reboot after
a short power release, corroborated by the PON/SCM log. Touch stopped before
a power request: both idle, empty active TCS had CONTROL=0. Attempt 14 now
uses the documented trigger-clear, mode-clear, mode-enable, trigger-set
sequence, each preparation write read back; unknown CONTROL bits still reject.
Its host tests cover this observed handoff and a failed mode-enable readback.
Attempt 14 then completed the RPMh request and observed the LDO ready at
1800 mV, FTS I2C ACK/READY and an ACKed normal-scan command. The user still
reported no touch response; no contact record was observed. Warm Restart
passed again. Further touch tests are deferred at the user's request while
planning the RAM payload, DarwinBoot and minimal kernel handoff.

## RAM payload profile

`--payload-profile ram` requires `--display-profile inherited --input-profile buttons`.
The default `none` preserves the earlier menu-only image. RAM mode adds source
EnglishDxe, DiskIoDxe, Fat and UmiRamPayloadDxe to the generated board. The
8-MiB FAT16 superfloppy is placed in the Android v1 boot image's ramdisk field.
The driver reads the ABL-patched initrd bounds from the original entry DTB,
accepting only the already reserved and mapped 0xA0000000..0xB0000000 window.
It rejects overlap, misalignment, invalid sizes and out-of-range block reads;
all block writes return EFI_WRITE_PROTECTED.

Attempt 15 observed DTB=0xAF5FE000 and initrd=0xAF7FF000..0xAFFFF000 (exclusive).
SimpleFileSystem opened `EFI/UniXNU/unixnu.toml` and the external
`Payloads/UmiPayloadCheck.efi`. The application is built separately and is
absent from the firmware volume, as enforced by the FD audit. It verified
16,384 pattern bytes, EOF and a 1,021-byte read at offset 509, CRC32 4660B6D4.
This is a Boot Services EFI application; DarwinBoot, ExitBootServices and
kernel handoff remain untested. The 51-MiB allocator heap is unchanged.

The archive `out/umi/boot/attempt-15/` pins the tested image, FD, FAT image,
sources and recovered console. mtools is required to build the payload.
Round-trip extraction checks file hashes; FAT metadata timestamps mean new
builds are not promised to be byte-identical to this archive.

## Original dispatch evidence and retained differences

The handset's extracted XBL `RawFiles/DXE_apriori_file` is 1344 bytes: 84
little-endian EFI GUIDs. SHA-256:
`8d0fc7193d28024d497d35f56c0256fb860b1b2cd3cbc3efa5dfa89937c5f73f`.
The manifest associates stock indices 41, 42 and 43 with SPI, I2C and
DisplayDxe. The stock dispatcher therefore ran DisplayDxe through APRIORI;
its DEPEX is not a complete description of its stock dispatch eligibility.

`Include/APRIORI.inc` retains the relative original order of the selected
vendor modules and uses source equivalents in the original core-service
slots. All imported PE and original DEPEX bytes remain unchanged. PCD and
UniXNUDxe are explicitly identified local additions. A machine-readable slot
mapping and omitted stock entries are in `Include/APRIORI-order.json`.

**This is a pruned subset with replacement core services, not an equivalent
stock XBL environment.** Preserving this order does not prove that any
selected binary can run with Mu's protocols or establish a GOP.

## Pre-display candidates and bounded static audit

PdcDxe (stock index 20) and SPI (index 41) are included at their stock slots.
They were missing from the initial subset. Their provenance and placement
make them reasonable pre-display candidates, but this analysis does not
establish that either is necessary or sufficient for this display path.
PdcDxe has original TRUE/END DEPEX (`0608`). SPI retains its original DEPEX;
its strings mention bus transfers, GPIO, clock control, `/dev/SPI` and
`spi_full_duplex`. These are static observations, not observed hardware calls.

FeatureEnablerDxe (index 38) is omitted. Its PE strings mention
`DisplayFeatureEnablerExecute`, `FeatureEnablerSendCommandTA`, loading and
unloading a trusted application through QSEE and display clock operations.
No source or execution evidence here establishes its side effects or that it
is safe to treat it as an information-only driver.

DDRInfoDxe (index 40) is omitted. Its strings include `DDR UEFI req client`,
NPA-protocol lookup and SMEM allocation messages. The driver name and those
strings do not prove an information-only entry point or read-only execution.
Its original DEPEX is TRUE/END (`0608`); this does not establish its behavior.
Both omissions are intentional differences from the stock pre-display chain.
Standard linked UEFI error strings such as `Warning Write Failure` were not
treated as evidence of an actual write call.

Audited PE SHA-256 values:

| Module | SHA-256 |
| --- | --- |
| PdcDxe | `f3e0cfae7393ffaa27990ef28755b68580ad236b5d085626305efb7b1457d54c` |
| SPI | `8c0a943468f9aa9e97adecd0cbc48322954adf21d6ca6c373f2ccc7c079dddf9` |
| FeatureEnablerDxe | `0bad5a385b43c48ee457410e9428198daab6ebce383da31735f9b6e690f8081f` |
| DDRInfoDxe | `ee354d5ce257d02cb41bbed0006c8b80ab2ce946ccac92262185a0479907422d` |

## Image boundaries

The base FDF includes no disk/partition/filesystem drivers; the explicit RAM
profile adds only the source RAM block and FAT services described above.
Neither profile includes UFS, SD, USB/MSC, PIL, VerifiedBoot, a nonvolatile
variable backend, RAM expansion driver, ACPI tables or firmware-update
applications. UEFI boot variables use RAM emulation.
`SecureBootHandlerLib` is a no-op, so it does not register a filesystem callback.
SEC early framebuffer access is disabled. The first attempt used SerialPortLib
Null; the current diagnostic version uses RamoopsSerialPortLib. Its console
window is the kernel-confirmed first 2 MiB at 0xB0000000; the entire 4 MiB
ramoops range is reserved and mapped normal uncached, and pmsg is not written.
BootShim places UMI0/UMI1 markers before/after copying the FD; SEC adds stage
messages. A warm reset may preserve these for Android console-ramoops-0, but
retention and reaching the entry point must be verified on every attempt. The
vendor-profile GOP comes from the unchanged local DisplayDxe; the inherited
profile uses the board's source GOP instead.
These selections bound this image's planned interfaces; opaque vendor
components have not been proved free of all hardware side effects.
