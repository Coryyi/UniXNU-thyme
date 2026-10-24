// SPDX-License-Identifier: BSD-2-Clause-Patent
// External test only. No persistent storage writes or XNU entry.
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>

#define VOLUME_BYTES SIZE_64MB
#define PROOF_BYTES  (60U * SIZE_1MB)
VOID PayloadSha256 (CONST UINT8 *Bytes, UINTN Length, UINT8 Digest[32]);

STATIC VOID HexDigest (CONST UINT8 Digest[32], CHAR8 Hex[65])
{
  STATIC CONST CHAR8 Digits[] = "0123456789abcdef";
  UINTN I;
  for (I = 0; I < 32; I++) {
    Hex[2 * I] = Digits[Digest[I] >> 4];
    Hex[2 * I + 1] = Digits[Digest[I] & 15];
  }
  Hex[64] = 0;
}

EFI_STATUS EFIAPI
UefiMain (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_BLOCK_IO_PROTOCOL *Block;
  EFI_FILE_PROTOCOL *Root = NULL, *File = NULL, *Manifest = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Green = {0x30, 0xB0, 0x20, 0};
  EFI_STATUS Status;
  UINT8 *Buffer = NULL, Digest[32], Scratch[1021];
  CHAR8 Hex[65], Expected[65];
  UINTN Size, At, I, Total = 0;
  STATIC CONST UINTN Seek[] = {509, SIZE_16MB - 509, SIZE_32MB + 17, PROOF_BYTES - 1021};
  (VOID)SystemTable;

  DEBUG ((DEBUG_WARN, "UMI payload64 BEGIN\n"));
  Print (L"Checking 64 MiB volume and 60 MiB file...\r\n");
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiBlockIoProtocolGuid, (VOID **)&Block);
  if (EFI_ERROR (Status)) { goto Done; }
  if (!Block->Media->ReadOnly || Block->Media->BlockSize != 512 ||
      Block->Media->LastBlock != VOLUME_BYTES / 512 - 1) {
    Status = EFI_COMPROMISED_DATA; goto Done;
  }
  Buffer = AllocatePool (VOLUME_BYTES);
  if (Buffer == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  // Read every block, including the very last sector, through BlockIo.
  for (At = 0; At < VOLUME_BYTES; At += SIZE_64KB) {
    Status = Block->ReadBlocks (Block, Block->Media->MediaId, At / 512, SIZE_64KB, Buffer + At);
    if (EFI_ERROR (Status)) { goto Done; }
  }
  PayloadSha256 (Buffer, VOLUME_BYTES, Digest);
  HexDigest (Digest, Hex);
  // Host compares this with the exact archived FAT image. Its expected hash
  // cannot be embedded in that same volume without creating a circular hash.
  DEBUG ((DEBUG_WARN, "UMI payload64 volume bytes=%u sha256=%a readonly=1 last-lba=%Lu\n",
          (UINT32)VOLUME_BYTES, Hex, Block->Media->LastBlock));
  Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) { goto Done; }
  Status = Root->Open (Root, &Manifest, L"\\EFI\\UniXNU\\Payloads\\proof-large.sha256", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { goto Done; }
  Size = sizeof (Expected);
  Status = Manifest->Read (Manifest, &Size, Expected);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Size != 64) { Status = EFI_COMPROMISED_DATA; goto Done; }
  Expected[64] = 0;
  Status = Root->Open (Root, &File, L"\\EFI\\UniXNU\\Payloads\\proof-large.bin", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) { goto Done; }
  while (Total < PROOF_BYTES) {
    // Non-sector and non-cluster aligned reads across the old size limit.
    Size = MIN (65521U, PROOF_BYTES - Total);
    At = Size;
    Status = File->Read (File, &Size, Buffer + Total);
    if (EFI_ERROR (Status)) { goto Done; }
    if (Size == 0 || Size > At) { Status = EFI_BAD_BUFFER_SIZE; goto Done; }
    Total += Size;
  }
  Size = sizeof (Scratch);
  Status = File->Read (File, &Size, Scratch);
  if (EFI_ERROR (Status)) { goto Done; }
  if (Size != 0) { Status = EFI_BAD_BUFFER_SIZE; goto Done; }
  PayloadSha256 (Buffer, PROOF_BYTES, Digest);
  HexDigest (Digest, Hex);
  if (CompareMem (Expected, Hex, 64) != 0) { Status = EFI_CRC_ERROR; goto Done; }
  DEBUG ((DEBUG_WARN, "UMI payload64 file bytes=%u sha256=%a EOF=ok\n", (UINT32)Total, Hex));
  for (I = 0; I < ARRAY_SIZE (Seek); I++) {
    Status = File->SetPosition (File, Seek[I]);
    if (EFI_ERROR (Status)) { goto Done; }
    Size = sizeof (Scratch);
    Status = File->Read (File, &Size, Scratch);
    if (EFI_ERROR (Status)) { goto Done; }
    if (Size != sizeof (Scratch) || CompareMem (Scratch, Buffer + Seek[I], Size) != 0) {
      Status = EFI_CRC_ERROR; goto Done;
    }
    DEBUG ((DEBUG_WARN, "UMI payload64 seek=%Lu bytes=%u PASS\n", (UINT64)Seek[I], (UINT32)Size));
  }
Done:
  if (Manifest != NULL) { Manifest->Close (Manifest); }
  if (File != NULL) { File->Close (File); }
  if (Root != NULL) { Root->Close (Root); }
  if (Buffer != NULL) { FreePool (Buffer); }
  DEBUG ((DEBUG_WARN, "UMI payload64 END status=%r file-bytes=%u\n", Status, (UINT32)Total));
  if (!EFI_ERROR (Status) &&
      !EFI_ERROR (gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop))) {
    Gop->Blt (Gop, &Green, EfiBltVideoFill, 0, 0, 0, 0,
              Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution, 0);
  }
  Print (L"64 MiB payload check: %r\r\nRestarting...\r\n", Status);
  gBS->Stall (3000000);
  gRT->ResetSystem (EfiResetWarm, EFI_SUCCESS, 0, NULL);
  return EFI_DEVICE_ERROR;
}
