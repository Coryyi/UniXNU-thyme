/** @file
  umi: publish the ABL-provided FAT initrd as a read-only RAM block device.
  The only accepted physical window is already reserved and MMU-mapped by
  this board's MemoryMapLib. No DRAM expansion or persistent device access.
  Protocol structure follows ShengBootMemoryDxe; no sheng addresses used.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/HobLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleFileSystem.h>

#define BOOT_BASE  0xA0000000ULL
#define BOOT_END   0xB0000000ULL
#define BLOCK_SIZE 512U
// Limit the input FAT image independently of the conventional Darwin RAM.
// The complete image must still fit the reserved 0xA0000000..0xB0000000
// window and remain disjoint from the ABL-adjusted device tree.
#define MAX_IMAGE  SIZE_128MB

STATIC UINT8 *mRamBase;
STATIC EFI_BLOCK_IO_MEDIA mMedia;
STATIC UINTN mReadCount;

STATIC BOOLEAN
InBootWindow (UINT64 Base, UINT64 Size)
{
  return (Base >= BOOT_BASE) && (Base < BOOT_END) &&
         (Size != 0) && (Size <= BOOT_END - Base);
}

STATIC EFI_STATUS EFIAPI
RamReset (EFI_BLOCK_IO_PROTOCOL *This, BOOLEAN ExtendedVerification)
{
  (VOID)This;
  (VOID)ExtendedVerification;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
RamRead (EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba,
         UINTN BufferSize, VOID *Buffer)
{
  (VOID)This;
  if (MediaId != mMedia.MediaId) {
    return EFI_MEDIA_CHANGED;
  }
  if (BufferSize == 0) {
    return EFI_SUCCESS;
  }
  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if ((BufferSize % BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }
  if ((Lba > mMedia.LastBlock) ||
      ((BufferSize / BLOCK_SIZE) > (mMedia.LastBlock - Lba + 1))) {
    return EFI_INVALID_PARAMETER;
  }
  CopyMem (Buffer, mRamBase + (UINTN)Lba * BLOCK_SIZE, BufferSize);
  if (mReadCount < 4) {
    DEBUG ((DEBUG_WARN, "UMI payload read lba=%Lu bytes=%Lu\n", Lba, (UINT64)BufferSize));
    mReadCount++;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
RamWrite (EFI_BLOCK_IO_PROTOCOL *This, UINT32 MediaId, EFI_LBA Lba,
          UINTN BufferSize, VOID *Buffer)
{
  (VOID)This; (VOID)MediaId; (VOID)Lba; (VOID)BufferSize; (VOID)Buffer;
  return EFI_WRITE_PROTECTED;
}

STATIC EFI_STATUS EFIAPI
RamFlush (EFI_BLOCK_IO_PROTOCOL *This)
{
  (VOID)This;
  return EFI_SUCCESS;
}

STATIC EFI_BLOCK_IO_PROTOCOL mBlockIo = {
  EFI_BLOCK_IO_PROTOCOL_REVISION, &mMedia, RamReset, RamRead, RamWrite, RamFlush
};

#pragma pack(1)
typedef struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} RAM_PATH;
#pragma pack()

STATIC RAM_PATH mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   {0x684577D8, 0x2902, 0x4B57, {0x9E, 0x05, 0x49, 0xF5, 0xB4, 0xBA, 0x4B, 0x57}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}
};

STATIC EFI_STATUS
ReadAddress (CONST VOID *Fdt, INT32 Node, CONST CHAR8 *Name, UINT64 *Address)
{
  INT32 Length;
  CONST VOID *Data = FdtGetProp (Fdt, Node, Name, &Length);
  if (Data == NULL) {
    return EFI_NOT_FOUND;
  }
  if (Length == 4) {
    *Address = Fdt32ToCpu (ReadUnaligned32 (Data));
  } else if (Length == 8) {
    *Address = Fdt64ToCpu (ReadUnaligned64 (Data));
  } else {
    return EFI_COMPROMISED_DATA;
  }
  return EFI_SUCCESS;
}

STATIC BOOLEAN
ValidInitrd (UINT64 Dtb, UINTN TreeSize, UINT64 Start, UINT64 End)
{
  return InBootWindow (Dtb, TreeSize) &&
         (End > Start) && InBootWindow (Start, End - Start) &&
         ((Start & (BLOCK_SIZE - 1)) == 0) && ((End - Start) % BLOCK_SIZE == 0) &&
         (End - Start <= MAX_IMAGE) && (End - Start >= SIZE_1MB) &&
         !((Start < Dtb + TreeSize) && (Dtb < End));
}

EFI_STATUS EFIAPI
UmiRamPayloadEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HOB_GUID_TYPE *Hob;
  EFI_STATUS Status;
  UINT64 Dtb, Start = 0, End = 0;
  UINTN TreeSize;
  INT32 Chosen;
  VOID *Fdt;
  EFI_HANDLE Handle = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  UINT32 Sectors;
  (VOID)ImageHandle; (VOID)SystemTable;

  Hob = GetFirstGuidHob (&gSiliciumEntryArgHobGuid);
  if ((Hob == NULL) || (GET_GUID_HOB_DATA_SIZE (Hob) < sizeof (Dtb))) {
    return EFI_NOT_FOUND;
  }
  Dtb = ReadUnaligned64 (GET_GUID_HOB_DATA (Hob));
  DEBUG ((DEBUG_WARN, "UMI payload entry DTB=%lx\n", Dtb));
  // Reject before dereference if ABL moved the tree outside mapped RAM.
  if (((Dtb & 7) != 0) || !InBootWindow (Dtb, 40)) {
    DEBUG ((DEBUG_WARN, "UMI payload DTB outside reserved mapped boot window\n"));
    return EFI_UNSUPPORTED;
  }
  Fdt = (VOID *)(UINTN)Dtb;
  if (FdtCheckHeader (Fdt) != 0) {
    return EFI_COMPROMISED_DATA;
  }
  TreeSize = FdtTotalSize (Fdt);
  if ((TreeSize < 40) || (TreeSize > SIZE_2MB) || !InBootWindow (Dtb, TreeSize)) {
    return EFI_COMPROMISED_DATA;
  }
  Chosen = FdtPathOffset (Fdt, "/chosen");
  if (Chosen < 0) {
    return EFI_NOT_FOUND;
  }
  Status = ReadAddress (Fdt, Chosen, "linux,initrd-start", &Start);
  if (!EFI_ERROR (Status)) {
    Status = ReadAddress (Fdt, Chosen, "linux,initrd-end", &End);
  }
  DEBUG ((DEBUG_WARN, "UMI payload initrd=%lx..%lx DTB-bytes=%lx max-bytes=%lx status=%r\n",
          Start, End, (UINT64)TreeSize, (UINT64)MAX_IMAGE, Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!ValidInitrd (Dtb, TreeSize, Start, End)) {
    return EFI_COMPROMISED_DATA;
  }
  mRamBase = (UINT8 *)(UINTN)Start;
  // A FAT superfloppy, not an Android compressed cpio or a partition table.
  Sectors = ReadUnaligned16 ((CONST UINT16 *)(mRamBase + 19));
  if (Sectors == 0) {
    Sectors = ReadUnaligned32 ((CONST UINT32 *)(mRamBase + 32));
  }
  if ((mRamBase[510] != 0x55) || (mRamBase[511] != 0xAA) ||
      (ReadUnaligned16 ((CONST UINT16 *)(mRamBase + 11)) != BLOCK_SIZE) ||
      ((UINT64)Sectors * BLOCK_SIZE != End - Start)) {
    return EFI_COMPROMISED_DATA;
  }
  mMedia.MediaId = 1;
  mMedia.MediaPresent = TRUE;
  mMedia.ReadOnly = TRUE;
  mMedia.BlockSize = BLOCK_SIZE;
  mMedia.IoAlign = 1;
  mMedia.LastBlock = Sectors - 1;
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle,
    &gEfiDevicePathProtocolGuid, &mPath, &gEfiBlockIoProtocolGuid, &mBlockIo, NULL);
  DEBUG ((DEBUG_WARN, "UMI payload RAM block bytes=%Lu read-only=1 status=%r\n",
          End - Start, Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->ConnectController (Handle, NULL, NULL, TRUE);
  DEBUG ((DEBUG_WARN, "UMI payload connect=%r\n", Status));
  Status = gBS->HandleProtocol (Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  DEBUG ((DEBUG_WARN, "UMI payload filesystem=%r\n", Status));
  // Keep the valid block device installed if a filesystem driver binds later.
  return EFI_SUCCESS;
}
