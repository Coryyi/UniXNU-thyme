# Source provenance

New umi integration is distributed under BSD-2-Clause-Patent (see LICENSE).
Existing source-file notices and upstream licenses continue to apply.

- Generic drivers, ABI declarations and build recipes derive from UniXNU at
  `7b52e7968c76453cdc18b5aa174fc368d5293b91`.
- Diagnostic display and related device integration derive from UniXNU-sheng at
  `a599a1a8a92093b5077543e071b72130769eb325`. Detailed file pins remain in
  `package/boot-progress/upstream.json` and `package/legacy-display/upstream.json`.
- Firmware overlays derive from uefi-sheng at
  `329b473df1e53a3bfa64b2b39b79670c4c02279b` and retain their file notices.
- The ST protocol and board wiring were checked against MiCode's public
  `umi-q-oss` kernel at `db67ca6001320efc1f945c270635434fc403a9d4` and the
  LineageOS SM8250 kernel. The original Linux driver is not bundled; references
  do not relicense its source. Additional references are in `SOURCES.txt` files.

Apple restore components and vendor firmware are external inputs, not licensed
or distributed by this repository. Local filesystem evidence references in
source notes describe private research records, not downloadable artifacts.
