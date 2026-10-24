// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/ConfigurationMapLib.h>

// Kona reference values retained only for non-storage display dependencies.
// The imported UEFI Config shim is the sole consumer-facing source of these
// entries. Filesystem log syncing and secondary-core/display threads are off.
STATIC EFI_CONFIGURATION_ENTRY_DESCRIPTOR gConfigurationDescriptor[] = {
  {"NumCpusFuseAddr", 0x5C04C},
  {"SharedIMEMBaseAddr", 0x146BF000},
  {"DloadCookieAddr", 0x01FD3000},
  {"DloadCookieValue", 0x10},
  {"SecPagePoolCount", 0x800},
  {"NumCpus", 8},
  {"NumActiveCores", 1},
  {"MaxCoreCnt", 8},
  {"EarlyInitCoreCnt", 1},
  {"MaxLogFileSize", 0x400000},
  {"UefiMemUseThreshold", 0x77},
  {"SecurityFlag", 0xC4},
  {"ShmBridgememSize", 0xA00000},
  {"PwrBtnShutdownFlag", 0},
  {"EnableShell", 0},
  {"EnableLogFsSyncInRetail", 0},
  {"EnableUefiSecAppDebugLogDump", 0},
  {"AllowNonPersistentVarsInRetail", 1},
  {"EnableMultiThreading", 0},
  {"EnableDisplayThread", 0},
  {"EnableDisplayImageFv", 0},
  {"EnableUfsIOC", 0},
  {"EnableSDHCSwitch", 0},
  {"USBHS1_Config", 0}
};

VOID
GetConfigurationMap (
  OUT EFI_CONFIGURATION_ENTRY_DESCRIPTOR **ConfigurationDescriptor,
  OUT UINT8                               *ConfigurationDescriptorCount)
{
  *ConfigurationDescriptor      = gConfigurationDescriptor;
  *ConfigurationDescriptorCount = ARRAY_SIZE (gConfigurationDescriptor);
}
