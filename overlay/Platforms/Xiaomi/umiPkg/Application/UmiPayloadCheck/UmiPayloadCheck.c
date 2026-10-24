/** @file
  External FAT payload: check a multi-cluster file on the image's own volume.
  Deliberately built but NOT included in the firmware volume.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>

EFI_STATUS EFIAPI
UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_BLOCK_IO_PROTOCOL *Block;
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Green = {0x30, 0xB0, 0x20, 0};
  EFI_STATUS Status;
  UINT8 Buffer[1021];
  UINTN Size, Index, Total = 0;
  UINT32 Crc;
  (VOID)SystemTable;

  DEBUG ((DEBUG_WARN, "UMI payload-check external EFI entered\n"));
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID **)&Block);
  if (EFI_ERROR (Status)) { goto Done; }
  if (!Block->Media->ReadOnly || Block->Media->BlockSize != 512) {
    Status = EFI_COMPROMISED_DATA; goto Done;
  }
  Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Root->Open (Root, &File, L"\\EFI\\UniXNU\\Payloads\\proof.bin", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { goto Done; }
  // Non-sector-aligned reads cross FAT clusters and cover the entire fixture.
  while (Total < 16384) {
    Size = sizeof (Buffer);
    if (Size > 16384 - Total) { Size = 16384 - Total; }
    Status = File->Read (File, &Size, Buffer);
    if (EFI_ERROR (Status)) { goto Done; }
    if (Size == 0) { Status = EFI_END_OF_FILE; goto Done; }
    for (Index = 0; Index < Size; Index++) {
      if (Buffer[Index] != (UINT8)((Total + Index) * 73 + 19)) {
        Status = EFI_CRC_ERROR; goto Done;
      }
    }
    Total += Size;
  }
  Size = sizeof (Buffer);
  Status = File->Read (File, &Size, Buffer);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Size != 0) { Status = EFI_BAD_BUFFER_SIZE; goto Done; }
  Status = File->SetPosition (File, 509);
  if (EFI_ERROR (Status)) { goto Done; }
  Size = sizeof (Buffer);
  Status = File->Read (File, &Size, Buffer);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Size != sizeof (Buffer)) { Status = EFI_BAD_BUFFER_SIZE; goto Done; }
  for (Index = 0; Index < Size; Index++) {
    if (Buffer[Index] != (UINT8)((509 + Index) * 73 + 19)) {
      Status = EFI_CRC_ERROR; goto Done;
    }
  }
  Status = gBS->CalculateCrc32 (Buffer, Size, &Crc);
  if (EFI_ERROR (Status)) { goto Done; }
  DEBUG ((DEBUG_WARN, "UMI payload-check PASS bytes=%u seek=509 size=%u crc32=%08x readonly=1 EOF=ok\n",
          (UINT32)Total, (UINT32)Size, Crc));
Done:
  if (File != NULL) { File->Close (File); }
  if (Root != NULL) { Root->Close (Root); }
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "UMI payload-check FAIL status=%r bytes=%u\n", Status, (UINT32)Total));
    Print (L"RAM payload check failed: %r\r\n", Status);
    gBS->Stall (2000000);
    return Status;
  }
  if (!EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) {
    Gop->Blt (Gop, &Green, EfiBltVideoFill, 0, 0, 0, 0,
              Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution, 0);
  }
  Print (L"RAM PAYLOAD PASS - restarting\r\n");
  gBS->Stall (3000000);
  gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  return EFI_DEVICE_ERROR;
}
