/** umi-only read-only SMMUv2 diagnostics; SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * Register offsets/fields checked against this device's kernel at
 * LineageOS/android_kernel_xiaomi_sm8250 commit
 * 71b13e62f057a649b77fe4062feb73ee72ad609c,
 * drivers/iommu/arm-smmu-regs.h, and the framework ArmSmmuDetachLib.
 * No fault clearing, translation changes, or page-table dereferences.
 */
#ifndef UMI_SMMU_TRACE_H
#define UMI_SMMU_TRACE_H

#include <Library/DebugLib.h>

STATIC UINT32
UmiSmmuRead32 (UINTN Offset)
{
  return *(volatile UINT32 *)(UINTN)(0x15000000ULL + Offset);
}

STATIC UINT64
UmiSmmuRead64 (UINTN Offset)
{
  return *(volatile UINT64 *)(UINTN)(0x15000000ULL + Offset);
}

STATIC VOID
UmiSmmuSnapshot (CONST CHAR8 *Stage)
{
  STATIC CONST UINT16 MdpStreams[] = { 0x820, 0x821, 0x822, 0xC20, 0xC21, 0xC22 };
  UINT32 Id0, Id1, Smr, S2cr, Mask, Sid, Bank, Sctlr, Fsr, Syn;
  UINTN Index, Stream, Shift, Pages, Offset;
  UINT64 Ttbr, Far;
  BOOLEAN Matches;

  DEBUG ((DEBUG_ERROR, "UMI SMMU %a begin\n", Stage));
  Id0 = UmiSmmuRead32 (0x20);
  Id1 = UmiSmmuRead32 (0x24);
  DEBUG ((DEBUG_ERROR, "UMI SMMU ID0=%08x ID1=%08x\n", Id0, Id1));
  if ((Id0 == 0) || (Id0 == MAX_UINT32) || (Id1 == MAX_UINT32)) {
    return;
  }
  Shift = (Id1 & BIT31) ? 16 : 12;
  Pages = 1U << (((Id1 >> 28) & 7U) + 1);
  for (Index = 0; Index < (Id0 & 0xFFU); Index++) {
    Smr = UmiSmmuRead32 (0x800 + Index * 4);
    if ((Smr & BIT31) == 0) {
      continue;
    }
    Mask = (Smr >> 16) & 0x7FFF;
    Sid = Smr & 0x7FFF;
    Matches = FALSE;
    for (Stream = 0; Stream < ARRAY_SIZE (MdpStreams); Stream++) {
      if ((MdpStreams[Stream] & ~Mask) == (Sid & ~Mask)) {
        Matches = TRUE;
      }
    }
    if (!Matches) {
      continue;
    }
    S2cr = UmiSmmuRead32 (0xC00 + Index * 4);
    Bank = S2cr & 0xFFU;
    DEBUG ((DEBUG_ERROR, "UMI SMMU slot=%u SMR=%08x S2CR=%08x CB=%u\n",
            (UINT32)Index, Smr, S2cr, Bank));
    // Only the firmware's already mapped 0xD0000 SMMU window is readable.
    Offset = (Pages + Bank) << Shift;
    if ((Bank >= (Id1 & 0xFFU)) || (Offset + 0x70 > 0xD0000)) {
      DEBUG ((DEBUG_ERROR, "UMI SMMU CB outside qualified window; skipped\n"));
      continue;
    }
    Sctlr = UmiSmmuRead32 (Offset);
    Ttbr = UmiSmmuRead64 (Offset + 0x20);
    Fsr = UmiSmmuRead32 (Offset + 0x58);
    Far = UmiSmmuRead64 (Offset + 0x60);
    Syn = UmiSmmuRead32 (Offset + 0x68);
    DEBUG ((DEBUG_ERROR, "UMI SMMU CB=%u SCTLR=%08x TTBR0=%016lx FSR=%08x FAR=%016lx FSYNR0=%08x\n",
            Bank, Sctlr, Ttbr, Fsr, Far, Syn));
  }
  DEBUG ((DEBUG_ERROR, "UMI SMMU %a end\n", Stage));
}
#endif
