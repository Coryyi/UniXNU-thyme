// SPDX-License-Identifier: BSD-2-Clause-Patent
// Half-open physical ranges; malformed nonempty ranges always fail closed.
#ifndef UMI_RAM_GUARD_H
#define UMI_RAM_GUARD_H
#define UMI_RAM_BASE  0xC0000000ULL
#define UMI_RAM_BYTES 0x20000000ULL
#define UMI_RAM_END   (UMI_RAM_BASE + UMI_RAM_BYTES)

STATIC BOOLEAN RamValid (UINT64 Base, UINT64 Size)
{
  return Size != 0 && Size <= (~(UINT64)0) - Base;
}

STATIC BOOLEAN RamContains (UINT64 Base, UINT64 Size, UINT64 Child, UINT64 Bytes)
{
  return RamValid (Base, Size) && RamValid (Child, Bytes) &&
         Child >= Base && Bytes <= Size && Child - Base <= Size - Bytes;
}

STATIC BOOLEAN RamOverlaps (UINT64 Base, UINT64 Size, UINT64 Other, UINT64 Bytes)
{
  if (!RamValid (Base, Size) || !RamValid (Other, Bytes)) { return TRUE; }
  return Base < Other + Bytes && Other < Base + Size;
}
#endif
