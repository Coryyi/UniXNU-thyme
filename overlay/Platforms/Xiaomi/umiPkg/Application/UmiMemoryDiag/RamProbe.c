// SPDX-License-Identifier: BSD-2-Clause-Patent
// Called only after maps were logged. No persistent storage operations.
#include <Uefi.h>
#include <PiDxe.h>
#include <Library/ArmMmuLib.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "SmemRead.h"
#include "RamGuard.h"

STATIC UINT64 Big64 (CONST VOID *P)
{
  return Fdt64ToCpu (ReadUnaligned64 (P));
}

STATIC BOOLEAN CellsTwo (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Size;
  CONST VOID *P = FdtGetProp (Fdt, Node, Name, &Size);
  return P != NULL && Size == 4 && Fdt32ToCpu (ReadUnaligned32 (P)) == 2;
}

STATIC EFI_STATUS CheckSmem (VOID)
{
  CONST volatile UINT8 *Smem = (VOID *)(UINTN)0x80900000, *T, *E;
  UINTN Offset, Size, Stride, I;
  UINT32 Count, Owners = 0;
  UINT64 Base, Length, Available;
  if (!SmemRamItem (Smem, 0x200000, &Offset, &Size)) { return EFI_NOT_FOUND; }
  T = Smem + Offset;
  if (!RamTableLayout (T, Size, &Count, &Stride) || Sm32 (T + 8) != 3) {
    return EFI_UNSUPPORTED;
  }
  for (I = 0; I < Count; I++) {
    E = T + 24 + I * Stride;
    Base = Sm64 (E + 16); Length = Sm64 (E + 24); Available = Sm64 (E + 64);
    if (!RamValid (Base, Length) || Available > Length) { return EFI_COMPROMISED_DATA; }
    if (!RamOverlaps (Base, Length, UMI_RAM_BASE, UMI_RAM_BYTES)) { continue; }
    if (Sm32 (E + 32) != 1 || Sm32 (E + 36) != 14 ||
        Sm32 (E + 40) > 1 || Sm32 (E + 44) != 1 ||
        !RamContains (Base, Available, UMI_RAM_BASE, UMI_RAM_BYTES)) {
      return EFI_ACCESS_DENIED;
    }
    Owners++;
  }
  return Owners == 1 ? EFI_SUCCESS : EFI_ACCESS_DENIED;
}

STATIC EFI_STATUS CheckFdt (VOID)
{
  // These are requests for allocations by the future Linux kernel, not
  // addresses allocated by ABL. Accept only the exact observed umi requests.
  STATIC CONST struct { CONST CHAR8 *Name; UINT64 Size; } Dynamic[] = {
    {"adsp_region",0xC00000}, {"sdsp_region",0x800000}, {"cdsp_region",0x400000},
    {"mem_dump_region",0x2800000}, {"sp_region",0x1000000},
    {"user_contig_region",0x1000000}, {"qseecom_region",0x1400000},
    {"qseecom_ta_region",0x1000000}, {"secure_display_region",0xA400000},
    {"cnss_wlan_region",0x1400000}, {"linux,cma",0x2000000},
    {"mailbox_region",0x20000}
  };
  EFI_HOB_GUID_TYPE *Hob = GetFirstGuidHob (&gSiliciumEntryArgHobGuid);
  CONST VOID *Fdt;
  CONST UINT8 *P;
  CONST CHAR8 *Name;
  UINT64 Address, Base, Length;
  UINTN Size, Offset, I, J;
  INT32 Node, Parent, Bytes, NameBytes;
  UINT32 Owners = 0, Seen = 0;
  if (Hob == NULL || GET_GUID_HOB_DATA_SIZE (Hob) < 8) { return EFI_NOT_FOUND; }
  Address = ReadUnaligned64 (GET_GUID_HOB_DATA (Hob));
  if ((Address & 7) || Address < 0xA0000000 || Address > 0xB0000000 - 40) {
    return EFI_UNSUPPORTED;
  }
  Fdt = (VOID *)(UINTN)Address;
  if (FdtCheckHeader (Fdt)) { return EFI_COMPROMISED_DATA; }
  Size = FdtTotalSize (Fdt);
  if (Size < 40 || Size > SIZE_2MB || Size > 0xB0000000 - Address ||
      !CellsTwo (Fdt, 0, "#address-cells") || !CellsTwo (Fdt, 0, "#size-cells")) {
    return EFI_COMPROMISED_DATA;
  }
  Node = FdtPathOffset (Fdt, "/memory");
  if (Node < 0) { return EFI_NOT_FOUND; }
  P = FdtGetProp (Fdt, Node, "reg", &Bytes);
  if (P == NULL || Bytes <= 0 || Bytes > 512 || Bytes % 16) { return EFI_COMPROMISED_DATA; }
  for (I = 0; I < (UINTN)Bytes; I += 16) {
    Base = Big64 (P + I); Length = Big64 (P + I + 8);
    if (!RamValid (Base, Length)) { return EFI_COMPROMISED_DATA; }
    if (RamContains (Base, Length, UMI_RAM_BASE, UMI_RAM_BYTES)) { Owners++; }
    else if (RamOverlaps (Base, Length, UMI_RAM_BASE, UMI_RAM_BYTES)) { return EFI_ACCESS_DENIED; }
  }
  if (Owners != 1) { return EFI_ACCESS_DENIED; }
  Parent = FdtPathOffset (Fdt, "/reserved-memory");
  if (Parent < 0 || !CellsTwo (Fdt, Parent, "#address-cells") ||
      !CellsTwo (Fdt, Parent, "#size-cells")) { return EFI_UNSUPPORTED; }
  P = FdtGetProp (Fdt, Parent, "ranges", &Bytes);
  if (P == NULL || Bytes != 0) { return EFI_UNSUPPORTED; }
  for (Node = FdtFirstSubnode (Fdt, Parent); Node >= 0; Node = FdtNextSubnode (Fdt, Node)) {
    P = FdtGetProp (Fdt, Node, "reg", &Bytes);
    if (P != NULL) {
      if (Bytes <= 0 || Bytes > 512 || Bytes % 16) { return EFI_COMPROMISED_DATA; }
      for (I = 0; I < (UINTN)Bytes; I += 16) {
        if (RamOverlaps (Big64 (P + I), Big64 (P + I + 8), UMI_RAM_BASE, UMI_RAM_BYTES)) {
          return EFI_ACCESS_DENIED;
        }
      }
      continue;
    }
    Name = FdtGetName (Fdt, Node, &NameBytes);
    if (Name == NULL) { return EFI_COMPROMISED_DATA; }
    for (J = 0; J < ARRAY_SIZE (Dynamic); J++) {
      if (AsciiStrCmp (Name, Dynamic[J].Name) == 0) { break; }
    }
    if (J == ARRAY_SIZE (Dynamic) || (Seen & (1U << J))) { return EFI_UNSUPPORTED; }
    Seen |= 1U << J;
    P = FdtGetProp (Fdt, Node, "size", &Bytes);
    if (P == NULL || Bytes != 8 || Big64 (P) != Dynamic[J].Size) { return EFI_UNSUPPORTED; }
    P = FdtGetProp (Fdt, Node, "alloc-ranges", &Bytes);
    if (P == NULL || Bytes != 16 || Big64 (P) != 0 || Big64 (P + 8) != 0xFFFFFFFF) {
      return EFI_UNSUPPORTED;
    }
  }
  if (Seen != (1U << ARRAY_SIZE (Dynamic)) - 1) { return EFI_UNSUPPORTED; }
  Offset = Fdt32ToCpu (ReadUnaligned32 ((CONST UINT32 *)((CONST UINT8 *)Fdt + 16)));
  while (Offset >= 40 && SmBounds (Offset, 16, Size)) {
    Base = Big64 ((CONST UINT8 *)Fdt + Offset);
    Length = Big64 ((CONST UINT8 *)Fdt + Offset + 8);
    if (Base == 0 && Length == 0) { return EFI_SUCCESS; }
    if (RamOverlaps (Base, Length, UMI_RAM_BASE, UMI_RAM_BYTES)) { return EFI_ACCESS_DENIED; }
    Offset += 16;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS CheckUnused (VOID)
{
  STATIC UINT64 Storage[8192];
  UINTN Size = sizeof (Storage), Key, Stride, I;
  UINT32 Version;
  EFI_STATUS Status;
  EFI_MEMORY_DESCRIPTOR *D;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Gcd;
  Status = gBS->GetMemoryMap (&Size, (VOID *)Storage, &Key, &Stride, &Version);
  if (EFI_ERROR (Status)) { return Status; }
  if (Stride < sizeof (*D) || Size > sizeof (Storage) || Size % Stride) {
    return EFI_COMPROMISED_DATA;
  }
  for (I = 0; I < Size; I += Stride) {
    D = (VOID *)((UINT8 *)Storage + I);
    if (D->NumberOfPages == 0) { continue; }
    if (D->NumberOfPages > MAX_UINT64 / EFI_PAGE_SIZE ||
        RamOverlaps (D->PhysicalStart, EFI_PAGES_TO_SIZE (D->NumberOfPages),
                     UMI_RAM_BASE, UMI_RAM_BYTES)) { return EFI_ACCESS_DENIED; }
  }
  Status = gDS->GetMemorySpaceDescriptor (UMI_RAM_BASE, &Gcd);
  if (EFI_ERROR (Status)) { return Status; }
  DEBUG ((DEBUG_WARN, "UMI ramprobe GCD type=%u base=%lx size=%lx\n",
          Gcd.GcdMemoryType, Gcd.BaseAddress, Gcd.Length));
  return Gcd.GcdMemoryType == EfiGcdMemoryTypeNonExistent &&
         RamContains (Gcd.BaseAddress, Gcd.Length, UMI_RAM_BASE, UMI_RAM_BYTES) ?
         EFI_SUCCESS : EFI_ACCESS_DENIED;
}

STATIC EFI_STATUS PatternTest (UINT64 Base, UINTN Bytes)
{
  volatile UINT64 *P = (VOID *)(UINTN)Base;
  UINTN I, Pass;
  UINT64 Salt, Expected;
  // Write the whole range before reading: also detects aliasing across pages
  // and across chunks. Flush/invalidate forces checks beyond the CPU cache.
  for (Pass = 0; Pass < 2; Pass++) {
    Salt = Pass ? 0x5AA53CC30FF09669ULL : 0xA55AC33CF00F6996ULL;
    DEBUG ((DEBUG_WARN, "UMI ramprobe pattern begin base=%lx bytes=%lx pass=%u\n",
            Base, (UINT64)Bytes, (UINT32)Pass));
    for (I = 0; I < Bytes / sizeof (*P); I++) {
      P[I] = Salt ^ (Base + I * sizeof (*P));
    }
    MemoryFence ();
    WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Base, Bytes);
    MemoryFence ();
    for (I = 0; I < Bytes / sizeof (*P); I++) {
      Expected = Salt ^ (Base + I * sizeof (*P));
      if (P[I] != Expected) {
        DEBUG ((DEBUG_ERROR, "UMI ramprobe pattern FAIL address=%lx pass=%u\n",
                Base + I * sizeof (*P), (UINT32)Pass));
        return EFI_DEVICE_ERROR;
      }
    }
    DEBUG ((DEBUG_WARN, "UMI ramprobe pattern PASS base=%lx bytes=%lx pass=%u\n",
            Base, (UINT64)Bytes, (UINT32)Pass));
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS AllocateDarwinStyle (UINT64 KernelOffset, EFI_PHYSICAL_ADDRESS *Address)
{
  STATIC UINT64 Storage[8192];
  UINTN Size = sizeof (Storage), Key, Stride, I, J;
  UINT32 Version;
  EFI_MEMORY_DESCRIPTOR *D, *Next;
  UINT64 End, Candidate;
  BOOLEAN Extended;
  EFI_STATUS Status;
  Status = gBS->GetMemoryMap (&Size, (VOID *)Storage, &Key, &Stride, &Version);
  if (EFI_ERROR (Status)) { return Status; }
  if (Stride < sizeof (*D) || Size > sizeof (Storage) || Size % Stride) {
    return EFI_COMPROMISED_DATA;
  }
  // Same requirement as DarwinBoot AllocateWindow: contiguous conventional
  // descriptors, 2 MiB rounding plus the kernel's low offset, EfiLoaderCode.
  for (I = 0; I < Size; I += Stride) {
    D = (VOID *)((UINT8 *)Storage + I);
    if (D->Type != EfiConventionalMemory || D->NumberOfPages == 0 ||
        D->NumberOfPages > (MAX_UINT64 - D->PhysicalStart) / EFI_PAGE_SIZE) { continue; }
    End = D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages);
    do {
      Extended = FALSE;
      for (J = 0; J < Size; J += Stride) {
        Next = (VOID *)((UINT8 *)Storage + J);
        if (Next->Type == EfiConventionalMemory && Next->PhysicalStart == End &&
            Next->NumberOfPages && Next->NumberOfPages <= (MAX_UINT64 - End) / EFI_PAGE_SIZE) {
          End += EFI_PAGES_TO_SIZE (Next->NumberOfPages);
          Extended = TRUE;
        }
      }
    } while (Extended);
    if (D->PhysicalStart > UMI_RAM_END) { continue; }
    Candidate = ALIGN_VALUE (D->PhysicalStart, SIZE_2MB) + KernelOffset;
    if (Candidate < D->PhysicalStart || End < Candidate || End - Candidate < SIZE_256MB ||
        !RamContains (UMI_RAM_BASE, UMI_RAM_BYTES, Candidate, SIZE_256MB)) { continue; }
    Status = gBS->AllocatePages (AllocateAddress, EfiLoaderCode,
                                 EFI_SIZE_TO_PAGES (SIZE_256MB), &Candidate);
    if (!EFI_ERROR (Status)) { *Address = Candidate; return Status; }
  }
  return EFI_OUT_OF_RESOURCES;
}

EFI_STATUS ProbeCandidateRam (VOID)
{
  EFI_STATUS Status, FreeStatus;
  EFI_PHYSICAL_ADDRESS Allocation = UMI_RAM_END - 1;
  EFI_PHYSICAL_ADDRESS Cursor;
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR Gcd;
  UINTN Pages = EFI_SIZE_TO_PAGES (SIZE_256MB);
  UINTN I;
  Status = CheckSmem ();
  DEBUG ((DEBUG_WARN, "UMI ramprobe guard SMEM=%r\n", Status));
  if (EFI_ERROR (Status)) { return Status; }
  Status = CheckFdt ();
  DEBUG ((DEBUG_WARN, "UMI ramprobe guard FDT=%r\n", Status));
  if (EFI_ERROR (Status)) { return Status; }
  Status = gDS->GetMemorySpaceDescriptor (UMI_RAM_BASE, &Gcd);
  if (EFI_ERROR (Status)) { return Status; }
  if (Gcd.GcdMemoryType == EfiGcdMemoryTypeNonExistent) {
    Status = CheckUnused ();
    DEBUG ((DEBUG_WARN, "UMI ramprobe guard unused=%r\n", Status));
    if (EFI_ERROR (Status)) { return Status; }
    Status = ArmSetMemoryAttributes (UMI_RAM_BASE, UMI_RAM_BYTES, EFI_MEMORY_WB | EFI_MEMORY_XP, 0);
    DEBUG ((DEBUG_WARN, "UMI ramprobe map=%r base=%lx bytes=%lx\n", Status, UMI_RAM_BASE, UMI_RAM_BYTES));
    if (EFI_ERROR (Status)) { return Status; }
    Status = PatternTest (UMI_RAM_BASE, UMI_RAM_BYTES);
    if (EFI_ERROR (Status)) { return Status; }
    Status = gDS->AddMemorySpace (EfiGcdMemoryTypeSystemMemory, UMI_RAM_BASE,
                                 UMI_RAM_BYTES, 0xF | EFI_MEMORY_XP);
    DEBUG ((DEBUG_WARN, "UMI ramprobe register=%r\n", Status));
    if (EFI_ERROR (Status)) { return Status; }
    Status = gDS->SetMemorySpaceAttributes (UMI_RAM_BASE, UMI_RAM_BYTES, EFI_MEMORY_WB | EFI_MEMORY_XP);
    DEBUG ((DEBUG_WARN, "UMI ramprobe attributes=%r\n", Status));
    if (EFI_ERROR (Status)) { return Status; }
  } else {
    // Code/data permission changes may split the GCD range. Walk every
    // descriptor instead of assuming a single descriptor spans all 512 MiB.
    for (Cursor = UMI_RAM_BASE; Cursor < UMI_RAM_END; Cursor = Gcd.BaseAddress + Gcd.Length) {
      Status = gDS->GetMemorySpaceDescriptor (Cursor, &Gcd);
      if (EFI_ERROR (Status)) { return Status; }
      if (Gcd.GcdMemoryType != EfiGcdMemoryTypeSystemMemory ||
          !RamContains (Gcd.BaseAddress, Gcd.Length, Cursor, 1)) {
        return EFI_ACCESS_DENIED;
      }
    }
    // Firmware already owns this range. Never overwrite its live allocations.
    DEBUG ((DEBUG_WARN, "UMI ramprobe registered-at-boot base=%lx bytes=%lx\n",
            UMI_RAM_BASE, UMI_RAM_BYTES));
  }
  for (I = 0; I < 2; I++) {
    Status = AllocateDarwinStyle (I == 0 ? 0 : 0x4000, &Allocation);
    DEBUG ((DEBUG_WARN, "UMI ramprobe DarwinAllocate256=%r base=%lx kernel-offset=%lx type=LoaderCode\n",
            Status, Allocation, I == 0 ? 0ULL : 0x4000ULL));
    if (EFI_ERROR (Status)) { return Status; }
    Status = PatternTest (Allocation, SIZE_256MB);
    FreeStatus = gBS->FreePages (Allocation, Pages);
    DEBUG ((DEBUG_WARN, "UMI ramprobe FreePages256=%r\n", FreeStatus));
    if (EFI_ERROR (Status)) { return Status; }
    if (EFI_ERROR (FreeStatus)) { return FreeStatus; }
  }
  return EFI_SUCCESS;
}
