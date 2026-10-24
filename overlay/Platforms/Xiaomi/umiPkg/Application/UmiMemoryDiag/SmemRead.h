// SPDX-License-Identifier: BSD-2-Clause-Patent
// Read-only, bounded decoding of Qualcomm SMEM metadata. Wire layout references:
// U-Boot drivers/smem/msm_smem.c (v11 global heap / v12 global partition).
// lk2nd fd11905d61f220daf9ad69b1b26144cbc6020169 platform/msm_shared/smem.h.
// Qualcomm RAM v3 wire layout: U-Boot patch by Aswin Murugan, 2026-04-17,
// https://www.mail-archive.com/u-boot@lists.denx.de/msg570801.html
// No shared-memory allocation, remote lock, payload access outside item 402,
// or writes to the source region.
#ifndef UMI_SMEM_READ_H
#define UMI_SMEM_READ_H

STATIC UINT16 Sm16 (CONST volatile UINT8 *P)
{
  return (UINT16)(P[0] | ((UINT16)P[1] << 8));
}

STATIC UINT32 Sm32 (CONST volatile UINT8 *P)
{
  return (UINT32)Sm16 (P) | ((UINT32)Sm16 (P + 2) << 16);
}

STATIC UINT64 Sm64 (CONST volatile UINT8 *P)
{
  return (UINT64)Sm32 (P) | ((UINT64)Sm32 (P + 4) << 32);
}

STATIC BOOLEAN SmBounds (UINTN Offset, UINTN Size, UINTN Limit)
{
  return (Offset <= Limit) && (Size <= Limit - Offset);
}

// Only the uncached global partition is supported for v12. Unsupported or
// malformed layouts are a diagnostic failure, never a reason to scan raw RAM.
STATIC BOOLEAN
SmemRamItem (CONST volatile UINT8 *Smem, UINTN Bytes, UINTN *Offset, UINTN *Size)
{
  UINTN Toc, Entry, I, Part, PartSize, Free, Cached, Pos, Data, Span, Padding;
  UINT32 Version, Count;
  if ((Bytes < 0x3000) || (Sm32 (Smem + 192) != 1) ||
      (Sm32 (Smem + 204) != 0)) { return FALSE; }
  Version = Sm32 (Smem + 64 + 7 * 4) >> 16;
  if (Version == 11) {
    Entry = 208 + 402 * 16;
    if (!SmBounds (Entry, 16, Bytes) || (Sm32 (Smem + Entry) != 1) ||
        ((Sm32 (Smem + Entry + 12) & 0xFFFFFFFCU) != 0)) { return FALSE; }
    Data = Sm32 (Smem + Entry + 4);
    Span = Sm32 (Smem + Entry + 8);
    if ((Data < 208 + 512 * 16) || (Span < 24) || (Span > 16384) ||
        !SmBounds (Data, Span, Bytes)) { return FALSE; }
    *Offset = Data; *Size = Span;
    return TRUE;
  }
  if (Version != 12) { return FALSE; }
  Toc = Bytes - 4096;
  if ((Sm32 (Smem + Toc) != 0x434F5424) ||
      (Sm32 (Smem + Toc + 4) != 1)) { return FALSE; }
  Count = Sm32 (Smem + Toc + 8);
  if ((Count == 0) || (Count > (4096 - 32) / 48)) { return FALSE; }
  Part = 0; PartSize = 0;
  for (I = 0; I < Count; I++) {
    Entry = Toc + 32 + I * 48;
    if ((Sm16 (Smem + Entry + 12) != 0xFFFE) ||
        (Sm16 (Smem + Entry + 14) != 0xFFFE)) { continue; }
    if (Part != 0) { return FALSE; } // Ambiguous duplicate global partitions.
    Part = Sm32 (Smem + Entry);
    PartSize = Sm32 (Smem + Entry + 4);
    if ((Part < 4096) || (PartSize < 32) || !SmBounds (Part, PartSize, Toc)) {
      return FALSE;
    }
  }
  if ((Part == 0) || (Sm32 (Smem + Part) != 0x54525024) ||
      (Sm16 (Smem + Part + 4) != 0xFFFE) ||
      (Sm16 (Smem + Part + 6) != 0xFFFE) ||
      (Sm32 (Smem + Part + 8) != PartSize)) { return FALSE; }
  Free = Sm32 (Smem + Part + 12);
  Cached = Sm32 (Smem + Part + 16);
  if ((Free < 32) || (Free > Cached) || (Cached > PartSize)) { return FALSE; }
  Pos = 32;
  while (Pos < Free) {
    Entry = Part + Pos;
    if (!SmBounds (Pos, 16, Free) || (Sm16 (Smem + Entry) != 0xA5A5)) {
      return FALSE;
    }
    Span = Sm32 (Smem + Entry + 4);
    Padding = Sm16 (Smem + Entry + 8);
    Data = Pos + 16 + Sm16 (Smem + Entry + 10);
    if ((Padding > Span) || !SmBounds (Data, Span, Free)) { return FALSE; }
    if (Sm16 (Smem + Entry + 2) == 402) {
      if ((Span - Padding < 24) || (Span - Padding > 16384)) { return FALSE; }
      *Offset = Part + Data; *Size = Span - Padding;
      return TRUE;
    }
    Pos = Data + Span;
  }
  return FALSE;
}

STATIC BOOLEAN
RamTableLayout (CONST volatile UINT8 *Table, UINTN Size, UINT32 *Count, UINTN *Stride)
{
  UINT32 Version;
  if ((Size < 24) || (Sm32 (Table) != 0x9DA5E0A8) ||
      (Sm32 (Table + 4) != 0xAF9EC4E2)) { return FALSE; }
  Version = Sm32 (Table + 8);
  if ((Version < 1) || (Version > 3)) { return FALSE; }
  *Stride = (Version == 1) ? 64 : 72;
  *Count = Sm32 (Table + 16);
  return (*Count > 0) && (*Count <= 32) && (*Count <= (Size - 24) / *Stride);
}
#endif
