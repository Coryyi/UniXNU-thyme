# thyme source-only UFS probe

This is a K0 diagnostic extension and preparation for roadmap K2 storage.
It does not satisfy K1 donor compatibility, APFS mounting or launchd acceptance.
The legacy full-prelink source-lock.json remains unchanged.

## Build

In Coryyi/UniXNU, dispatch macos.yml from the reviewed source commit with:

- device_repository: Coryyi/UniXNU-thyme
- device_ref: the exact reviewed device commit
- defconfig: thyme_k0_defconfig
- steps: kernel thyme-ufs-probe

The build checks the explicit main source revision and corecrypto startup fix,
creates a fresh workspace and builds bootstrap records, source providers and
libUmiUfsProbe.a, then links a source-only kernel. No IPSW, UEFI checkout,
historical QEMU receipt or full IOStorageFamily ABI snapshot is needed here.
The source build runs the existing SMMU host harness and instruction audits.
The output is a kernel and receipts, not a bootable Android image.
The source-only profile uses --ufs-screen-report: the phone transport is enabled
while old physical RAM-console trace/mapping code stays disabled. It preserves
the measured K0 entry path; panic-only packaging supplies visible receipts.

## Packaging and temporary phone experiment

Before packaging, regenerate the ADT from the thyme hardware profile. The old
thyme-adt.bin carrying /product/model=umi will be refused by this probe.
Keep the already qualified single-CPU allocation and panic-screen instrumentation.
Any final instruction patches must be followed by source-corecrypto HMAC resealing.
Do not reuse the full xnu-prelink menu for this source-only kernel.

First use umi-ufs-probe=1 thyme-ufs-report=1. Mode 1 reads controller registers;
it neither takes over storage DMA nor submits disk commands. The optional
THYME_UFS_REPORT panic is an intentional screen receipt, not an unexpected fault.
Confirm the existing panic-screen renderer is present in the final image.

Only after examining that receipt consider mode 2. Mode 2 changes controller
queues/SMMU/DMA state and submits NOP, query, wake, readiness, capacity and read
commands. It reads GPT plus the first userdata block; it does not publish a block
provider or mount Android userdata. The generated command layer rejects data
transfers from host to device (direction 1), and the persistent-write API is cut
out. Controller-register writes still occur in mode 2.

Accept mode 2 only if read stage 15 succeeds, the GPT CRCs match independently
collected evidence and userdata bounds are 0x2a8000..0x3b037fa with 4096-byte LBAs.
A stage 16 success additionally SHA-256 checks the 4096-byte primary GPT header
block at LBA 1 against the root ADB Recovery capture from this exact handset.
The pinned digest is in UmiUfsRead.inc; it is invalid if the GPT changes.
A missing Apple root manifest on the Android partition is expected.
Use temporary fastboot boot only; no partition flashing is part of this recipe.
