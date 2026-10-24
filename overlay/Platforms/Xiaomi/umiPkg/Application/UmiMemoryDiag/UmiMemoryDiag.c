/** @file
  External RAM-volume diagnostic. Observe maps before changing allocator policy.
  Validates SMEM/FDT/EFI/GCD before testing and registering candidate DRAM.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <PiDxe.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include "SmemRead.h"

#define SMEM_BASE 0x80900000ULL
#define SMEM_SIZE 0x00200000U
#define BOOT_BASE 0xA0000000ULL
#define BOOT_END  0xB0000000ULL

STATIC UINT64 mMapStorage[8192];
EFI_STATUS ProbeCandidateRam (VOID);

STATIC EFI_STATUS
DumpEfiMap (VOID)
{
  UINTN Size = sizeof (mMapStorage), Key, Stride, Offset, Other;
  UINT32 Version;
  EFI_STATUS Status;
  EFI_MEMORY_DESCRIPTOR *Map = (VOID *)mMapStorage, *D, *Next;
  UINT64 End, BestBase = 0, BestSize = 0, Total = 0;
  BOOLEAN Extended;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &Stride, &Version);
  DEBUG ((DEBUG_WARN, "UMI memdiag EFI status=%r bytes=%Lu stride=%Lu version=%u\n",
          Status, (UINT64)Size, EFI_ERROR (Status) ? 0 : (UINT64)Stride,
          EFI_ERROR (Status) ? 0 : Version));
  if (EFI_ERROR (Status)) { return Status; }
  if ((Stride < sizeof (*D)) || (Size % Stride != 0) || (Size > sizeof (mMapStorage))) {
    return EFI_COMPROMISED_DATA;
  }
  for (Offset = 0; Offset < Size; Offset += Stride) {
    D = (VOID *)((UINT8 *)Map + Offset);
    DEBUG ((DEBUG_WARN, "UMI memdiag EFI type=%u base=%lx pages=%lx attr=%lx\n",
            D->Type, D->PhysicalStart, D->NumberOfPages, D->Attribute));
    if (D->Type != EfiConventionalMemory || D->NumberOfPages == 0 ||
        D->NumberOfPages > (MAX_UINT64 - D->PhysicalStart) / EFI_PAGE_SIZE) { continue; }
    End = D->PhysicalStart + EFI_PAGES_TO_SIZE (D->NumberOfPages);
    Total += EFI_PAGES_TO_SIZE (D->NumberOfPages);
    do {
      Extended = FALSE;
      for (Other = 0; Other < Size; Other += Stride) {
        Next = (VOID *)((UINT8 *)Map + Other);
        if (Next->Type == EfiConventionalMemory && Next->PhysicalStart == End &&
            Next->NumberOfPages != 0 && Next->NumberOfPages <= (MAX_UINT64 - End) / EFI_PAGE_SIZE) {
          End += EFI_PAGES_TO_SIZE (Next->NumberOfPages);
          Extended = TRUE;
        }
      }
    } while (Extended);
    if (End - D->PhysicalStart > BestSize) {
      BestBase = D->PhysicalStart; BestSize = End - D->PhysicalStart;
    }
  }
  DEBUG ((DEBUG_WARN, "UMI memdiag EFI free-total=%lx largest-run=%lx at=%lx\n",
          Total, BestSize, BestBase));
  return EFI_SUCCESS;
}

STATIC VOID
DumpFdtProperty (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name)
{
  INT32 Bytes, I, NameLength;
  CONST UINT8 *Data = FdtGetProp (Fdt, Node, Name, &Bytes);
  CONST CHAR8 *NodeName = FdtGetName (Fdt, Node, &NameLength);
  if (Data == NULL || NodeName == NULL) { return; }
  if (Bytes < 0 || Bytes > 512 || (Bytes & 3)) {
    DEBUG ((DEBUG_WARN, "UMI memdiag FDT invalid property=%a length=%d\n", Name, Bytes));
    return;
  }
  DEBUG ((DEBUG_WARN, "UMI memdiag FDT node=%a property=%a bytes=%d\n", NodeName, Name, Bytes));
  for (I = 0; I < Bytes; I += 4) {
    DEBUG ((DEBUG_WARN, "UMI memdiag cell %u=%08x\n", I / 4,
            Fdt32ToCpu (ReadUnaligned32 ((CONST UINT32 *)(Data + I)))));
  }
}

STATIC EFI_STATUS
DumpFdt (VOID)
{
  EFI_HOB_GUID_TYPE *Hob = GetFirstGuidHob (&gSiliciumEntryArgHobGuid);
  UINT64 Address, Base, Length;
  UINTN Size, Offset;
  VOID *Fdt;
  INT32 Node, Parent;
  if (Hob == NULL || GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (Address)) { return EFI_NOT_FOUND; }
  Address = ReadUnaligned64 (GET_GUID_HOB_DATA (Hob));
  if ((Address & 7) || Address < BOOT_BASE || Address > BOOT_END - 40) { return EFI_UNSUPPORTED; }
  Fdt = (VOID *)(UINTN)Address;
  if (FdtCheckHeader (Fdt)) { return EFI_COMPROMISED_DATA; }
  Size = FdtTotalSize (Fdt);
  if (Size < 40 || Size > SIZE_2MB || Size > BOOT_END - Address) { return EFI_COMPROMISED_DATA; }
  DEBUG ((DEBUG_WARN, "UMI memdiag FDT base=%lx bytes=%lx\n", Address, (UINT64)Size));
  Node = FdtPathOffset (Fdt, "/memory");
  if (Node < 0) { return EFI_NOT_FOUND; }
  DumpFdtProperty (Fdt, Node, "reg");
  Node = FdtPathOffset (Fdt, "/chosen");
  if (Node >= 0) {
    DumpFdtProperty (Fdt, Node, "linux,initrd-start");
    DumpFdtProperty (Fdt, Node, "linux,initrd-end");
  }
  Parent = FdtPathOffset (Fdt, "/reserved-memory");
  if (Parent < 0) { return EFI_NOT_FOUND; }
  for (Node = FdtFirstSubnode (Fdt, Parent); Node >= 0; Node = FdtNextSubnode (Fdt, Node)) {
    DumpFdtProperty (Fdt, Node, "reg");
    DumpFdtProperty (Fdt, Node, "size");
    DumpFdtProperty (Fdt, Node, "alloc-ranges");
  }
  Offset = Fdt32ToCpu (ReadUnaligned32 ((UINT32 *)((UINT8 *)Fdt + 16)));
  while (Offset >= 40 && SmBounds (Offset, 16, Size)) {
    Base = Fdt64ToCpu (ReadUnaligned64 ((UINT64 *)((UINT8 *)Fdt + Offset)));
    Length = Fdt64ToCpu (ReadUnaligned64 ((UINT64 *)((UINT8 *)Fdt + Offset + 8)));
    if (Base == 0 && Length == 0) { return EFI_SUCCESS; }
    DEBUG ((DEBUG_WARN, "UMI memdiag FDT memreserve base=%lx size=%lx\n", Base, Length));
    Offset += 16;
  }
  return EFI_COMPROMISED_DATA;
}

STATIC EFI_STATUS
DumpSmem (VOID)
{
  CONST volatile UINT8 *Smem = (VOID *)(UINTN)SMEM_BASE, *Table, *Entry;
  UINTN Offset = 0, Size = 0, Stride, I;
  UINT32 Count;
  UINT64 Base, Length, Available;
  MemoryFence ();
  DEBUG ((DEBUG_WARN, "UMI memdiag SMEM base=%lx init=%u version=%08x\n",
          SMEM_BASE, Sm32 (Smem + 192), Sm32 (Smem + 92)));
  if (!SmemRamItem (Smem, SMEM_SIZE, &Offset, &Size)) {
    DEBUG ((DEBUG_WARN, "UMI memdiag SMEM item402 unavailable/unsupported; no RAM registered\n"));
    return EFI_NOT_FOUND;
  }
  Table = Smem + Offset;
  DEBUG ((DEBUG_WARN, "UMI memdiag SMEM item402 offset=%lx bytes=%lx magic=%08x/%08x version=%u count=%u\n",
          (UINT64)Offset, (UINT64)Size, Sm32 (Table), Sm32 (Table + 4),
          Sm32 (Table + 8), Sm32 (Table + 16)));
  if (!RamTableLayout (Table, Size, &Count, &Stride)) { return EFI_UNSUPPORTED; }
  for (I = 0; I < Count; I++) {
    Entry = Table + 24 + I * Stride;
    Base = Sm64 (Entry + 16); Length = Sm64 (Entry + 24);
    Available = (Stride == 72) ? Sm64 (Entry + 64) : Length;
    if (Length == 0 || Length > MAX_UINT64 - Base || Available > Length) {
      DEBUG ((DEBUG_WARN, "UMI memdiag RAM invalid entry=%u\n", (UINT32)I));
      return EFI_COMPROMISED_DATA;
    }
    DEBUG ((DEBUG_WARN, "UMI memdiag RAM entry=%u base=%lx size=%lx available=%lx attr=%u category=%u domain=%u type=%u\n",
            (UINT32)I, Base, Length, Available, Sm32 (Entry + 32), Sm32 (Entry + 36),
            Sm32 (Entry + 40), Sm32 (Entry + 44)));
  }
  MemoryFence ();
  return EFI_SUCCESS;
}

EFI_STATUS EFIAPI
UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS MapStatus, FdtStatus, SmemStatus, ProbeStatus = EFI_ABORTED;
  (VOID)ImageHandle; (VOID)SystemTable;
  DEBUG ((DEBUG_WARN, "UMI memdiag v4 BEGIN Darwin-allocation-check candidate=C0000000..E0000000\n"));
  Print (L"Reading memory maps...\r\n");
  MapStatus = DumpEfiMap ();
  FdtStatus = DumpFdt ();
  SmemStatus = DumpSmem ();
  if (!EFI_ERROR (MapStatus) && !EFI_ERROR (FdtStatus) && !EFI_ERROR (SmemStatus)) {
    Print (L"Verifying RAM and Darwin-style 256 MiB allocations...\r\n");
    ProbeStatus = ProbeCandidateRam ();
    DumpEfiMap ();
  }
  DEBUG ((DEBUG_WARN, "UMI memdiag v4 END efi=%r fdt=%r smem=%r probe=%r\n",
          MapStatus, FdtStatus, SmemStatus, ProbeStatus));
  Print (L"Memory test: %r\r\nRestarting...\r\n", ProbeStatus);
  gBS->Stall (3000000);
  gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  return EFI_DEVICE_ERROR;
}
