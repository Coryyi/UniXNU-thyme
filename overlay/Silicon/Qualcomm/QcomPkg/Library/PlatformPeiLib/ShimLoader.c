#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/ConfigurationMapLib.h>
#include <Library/ConfigurationMapHelperLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/SerialPortLib.h>

#include "ShimLibraries/EFIShim.h"
#include "ShimLibraries/EFISerialPort.h"
#include "ShimLibraries/EFIUefiConfig.h"
#include "PlatformPei.h"

// umi-only diagnostics: keep return values and ABI of the genuine config shim.
STATIC EFI_STATUS UmiLocateRegion (CHAR8 *Name, EFI_MEMORY_REGION_DESCRIPTOR *Region)
{
  EFI_STATUS Status;
  DEBUG ((EFI_D_ERROR, "UMI config region: %a\n", Name));
  Status = LocateMemoryRegionByName (Name, Region);
  DEBUG ((EFI_D_ERROR, "UMI config region returned %r\n", Status));
  return Status;
}
STATIC EFI_STATUS UmiConfig32 (CHAR8 *Name, UINT32 *Value)
{
  EFI_STATUS Status;
  DEBUG ((EFI_D_ERROR, "UMI config32: %a\n", Name));
  Status = LocateConfigurationEntry32 (Name, Value);
  DEBUG ((EFI_D_ERROR, "UMI config32 returned %r\n", Status));
  return Status;
}
STATIC EFI_STATUS UmiConfig64 (CHAR8 *Name, UINT64 *Value)
{
  EFI_STATUS Status;
  DEBUG ((EFI_D_ERROR, "UMI config64: %a\n", Name));
  Status = LocateConfigurationEntry64 (Name, Value);
  DEBUG ((EFI_D_ERROR, "UMI config64 returned %r\n", Status));
  return Status;
}

//
// UEFI Config Library
//
EFI_UEFI_CONFIG_LIBRARY
ConfigLib = {
  0x10002,
  UmiLocateRegion,
  LocateConfigurationEntryString,
  UmiConfig32,
  UmiConfig64, 
  LocateMemoryRegionByAddress
};

//
// Serial Port Library
//
EFI_SERIAL_PORT_LIBRARY
SioLib = {
  0x10001,
  SerialPortRead,
  SerialPortWrite,
  SerialPortPoll,
  SerialPortDrain,
  SerialPortFlush,
  SerialPortControl,
  SerialPortSetAttributes
};

EFI_STATUS
ShimInstallLib (
  IN CHAR8  *LibName,
  IN UINT32  LibVersion,
  IN VOID   *LibIntf)
{
  return EFI_SUCCESS;
}

EFI_SHIM_LIBRARY_INSTANCE_DATA
gShimLibraryInstanceData[] = {
  // Library Instance Name, Library Instance Pointer
  {"UEFI Config Lib", &ConfigLib},
  {"SerialPort Lib",  &SioLib}
};

EFI_STATUS
ShimLoadLib (
  IN  CHAR8   *LibName,
  IN  UINT32   LibVersion,
  OUT VOID   **LibIntf)
{
  DEBUG ((EFI_D_ERROR, "UMI shim load: %a version 0x%x\n", LibName, LibVersion));
  // Very Parameters
  if (LibName == NULL || LibIntf == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Go thru each Shim Library Instance
  for (UINT8 i = 0; i < ARRAY_SIZE (gShimLibraryInstanceData); i++) {
    // Compare Library Instance Names
    if (AsciiStriCmp (LibName, gShimLibraryInstanceData[i].LibName)) {
      continue;
    }

    // Pass Library Instance
    *LibIntf = gShimLibraryInstanceData[i].LibIntf;

    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}
