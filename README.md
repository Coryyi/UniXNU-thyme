# UniXNU-thyme — Xiaomi Mi 10S

[简体中文](README.zh-CN.md)

Experimental Mi 10S (`thyme`, SM8250-AC) port based on the Mi 10 (`umi`) work.
This repository does not yet demonstrate Hello, a home screen, working XNU touch,
or a usable iPadOS installation on thyme.

On 2026-10-25, an 8 GiB thyme was inspected through Android ADB and temporarily
booted into TWRP with root ADB. FDT, memory, GPT and kernel evidence were saved
locally. No partition flashing was performed. Recovery pstore was empty and
`/dev/mem` absent, so no XNU runtime trace was obtained.

The former Hello/touch/attempt-80 claims describe the upstream 12 GiB Mi 10.
They remain in [the historical upstream validation record](docs/validation-umi-upstream.json),
separate from [thyme observations](docs/validation.json).

The framebuffer/watchdog trace extension was removed after finding a repeated
L1-slot rejection, an unmapped framebuffer write, and physical accesses after
TTBR0 teardown. The console-only writer passes eight compiled ARM64 emulation
cases. A complete corrected kernel and phone boot still need validation.

See [build status and next gates](docs/BUILD.md). The original Mi 10 build guide
is preserved as [historical reference](docs/BUILD-umi-upstream.md).
The local staged `thymePkg` firmware and DarwinBoot changes must also be captured
before this can be reproduced from clean clones. `overlay/.../umiPkg`, the
upstream hardware JSON and `source-lock.json` are not thyme qualification.

Source kernel: XNU 10063.141.1. Donor: iPad7,1 / iPadOS 17.7.11 / 21H461.
Default configuration disables native UFS/root/Data/display integration; a
successful kernel CI job is not a complete bootable iPadOS system.

Existing credits and file licenses remain in [NOTICE.md](NOTICE.md) and [LICENSE](LICENSE).
