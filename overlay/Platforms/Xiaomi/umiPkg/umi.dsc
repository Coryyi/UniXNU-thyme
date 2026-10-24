## @file
# Xiaomi Mi 10 (umi), SM8250: temporary UEFI menu bring-up.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

[Defines]
  PLATFORM_NAME                  = umi
  PLATFORM_GUID                  = 0AA866E8-88D2-4CCD-910E-E43689D65E5B
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/umiPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = umiPkg/umi.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 1
  SOC_TYPE                       = 0

!include KonaPkg/KonaPkg.dsc.inc

[BuildOptions]
  # The port must not draw through a guessed SEC framebuffer. SEC.c consumes
  # this opt-out; the unmodified stock DisplayDxe publishes the real GOP.
  *_CLANGPDB_AARCH64_CC_FLAGS = -D UMI_SKIP_EARLY_FRAMEBUFFER=1 -D UNIXNU_BRINGUP_MENU=1

[PcdsFixedAtBuild]
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x9FF90000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Mi 10"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"umi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"umi-research"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"umi"
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2340
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

  # UniXNUDxe writes standard boot variables. Keep their backend in RAM.
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
  gEfiMdeModulePkgTokenSpaceGuid.PcdResetOnMemoryTypeInformationChange|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdCapsuleInRamSupport|FALSE
  gUniXNUPkgTokenSpaceGuid.PcdUniXNUDebugLevel|0x80000000

[PcdsFeatureFlag]
  gEfiMdeModulePkgTokenSpaceGuid.PcdSupportUpdateCapsuleReset|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSupportProcessCapsuleAtRuntime|FALSE

[LibraryClasses]
  MemoryMapLib|umiPkg/Library/MemoryMapLib/MemoryMapLib.inf
  ConfigurationMapLib|umiPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf
  SerialPortLib|umiPkg/Library/RamoopsSerialPortLib/RamoopsSerialPortLib.inf
  DeviceBootManagerLib|SiliciumNullPkg/Library/PlatformBootManagerLibNull/PlatformBootManagerLibNull.inf
  PlatformBootOptionsLib|SiliciumNullPkg/Library/PlatformBootOptionsLibNull/PlatformBootOptionsLibNull.inf
  SecureBootHandlerLib|umiPkg/Library/NullSecureBootHandlerLib/NullSecureBootHandlerLib.inf
  AcpiTableUpdateLib|SiliciumNullPkg/Library/AcpiTableUpdateLibNull/AcpiTableUpdateLibNull.inf
  RamManagerLib|SiliciumNullPkg/Library/RamManagerLibNull/RamManagerLibNull.inf
  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  CapsuleLib|MdeModulePkg/Library/DxeCapsuleLibNull/DxeCapsuleLibNull.inf

[Components]
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
