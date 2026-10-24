// SPDX-License-Identifier: BSD-2-Clause-Patent
#ifndef UMI_RPMH_LIB_H
#define UMI_RPMH_LIB_H
#include <Uefi.h>

// Caller must first identify this handset's LDO1, observe aggregate enable=0
// and an existing 1800 mV setting. No arbitrary resource/address API exists.
EFI_STATUS UmiRpmhTouchEnable (VOID);
// Sends enable=0 for our current DRV request. This does not reconstruct an
// unknown inherited DRV vote. Failed/inflight cleanup remains outstanding.
EFI_STATUS UmiRpmhTouchDisable (VOID);
BOOLEAN UmiRpmhTouchOutstanding (VOID);

#endif
