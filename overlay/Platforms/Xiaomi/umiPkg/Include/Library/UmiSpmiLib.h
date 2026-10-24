// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef UMI_SPMI_LIB_H
#define UMI_SPMI_LIB_H
#include <Uefi.h>

EFI_STATUS UmiSpmiRead8 (UINT8 Sid, UINT16 Address, UINT8 *Value);
// Write whitelist: primary PON reset registers and odd-SID LDO1 enable only.
// Callers must qualify the PMIC identity, peripheral and intended value first.
EFI_STATUS UmiSpmiWrite8 (UINT8 Sid, UINT16 Address, UINT8 Value);

#endif
