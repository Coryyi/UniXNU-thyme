// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/SecureBootHandlerLib.h>

// Menu bring-up does not enroll keys or install a filesystem callback.
EFI_STATUS
SetupSecureBoot (VOID)
{
  return EFI_SUCCESS;
}
