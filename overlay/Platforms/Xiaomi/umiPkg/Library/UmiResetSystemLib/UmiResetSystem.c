/** @file
  umi PM8150 primary PON / Qualcomm PS_HOLD reset.
  Fixed reference: LineageOS SM8250 71b13e62, msm-poweroff.c,
  qpnp-power-on.c and drivers/soc/qcom/scm.c. See out/umi/input-reference.
  PSCI SYSTEM_RESET hung on this handset in attempts 10/11.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UmiSpmiLib.h>

// Live DT /soc/restart@c264000, reg-names "tcsr-boot-misc-detect".
// This is passed to secure scm_io_write(), never accessed as direct MMIO.
#define UMI_TCSR_BOOT_MISC_DETECT  0x01FD3000U

STATIC BOOLEAN
ScmCall (UINT32 Function, UINTN ArgumentInfo, UINTN A, UINTN B, UINTN *Result)
{
  ARM_SMC_ARGS Args;
  UINTN Retry;
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = Function;
  Args.Arg1 = ArgumentInfo;
  Args.Arg2 = A;
  Args.Arg3 = B;
  DEBUG ((DEBUG_ERROR, "UMI reset: SCM %08x args=%lx,%lx,%lx\n", Function, ArgumentInfo, A, B));
  for (Retry = 0; Retry < 32; Retry++) {
    ArmCallSmc (&Args);
    // SCM_INTERRUPTED: resume with returned registers, including x6.
    if (Args.Arg0 != 1) {
      break;
    }
  }
  DEBUG ((DEBUG_ERROR, "UMI reset: SCM returned %lx,%lx\n", Args.Arg0, Args.Arg1));
  *Result = Args.Arg1;
  return Args.Arg0 == 0;
}

STATIC BOOLEAN
ScmAvailable (UINT32 Mask, UINT32 Function)
{
  UINTN Result;
  return ScmCall (0x02000601 | Mask, 1, Function, 0, &Result) && (Result != 0);
}

STATIC BOOLEAN
ClearDload (UINT32 Mask, BOOLEAN NativeDload)
{
  UINTN Result;

  if (NativeDload) {
    return ScmCall (0x82000110 | Mask, 2, 0, 0, &Result);
  }
  // Fixed msm-poweroff.c:147-162 falls back to scm_io_write() when the
  // native dload call is unavailable. Fixed scm.c:537-563 defines IO svc 5,
  // write cmd 2, SCM_ARGS(2), args={physical address, value}. Both sources
  // are pinned by out/umi/input-reference/reset-sources.json (71b13e62).
  DEBUG ((DEBUG_ERROR, "UMI reset: clear dload via secure TCSR write %08x=0\n",
          UMI_TCSR_BOOT_MISC_DETECT));
  return ScmCall (0x82000502 | Mask, 2, UMI_TCSR_BOOT_MISC_DETECT, 0, &Result);
}

STATIC EFI_STATUS
WriteVerify (UINT16 Address, UINT8 Value)
{
  UINT8 Actual;
  EFI_STATUS Status = UmiSpmiWrite8 (0, Address, Value);
  if (!EFI_ERROR (Status)) {
    Status = UmiSpmiRead8 (0, Address, &Actual);
    if (!EFI_ERROR (Status) && (Actual != Value)) {
      Status = EFI_DEVICE_ERROR;
    }
  }
  DEBUG ((DEBUG_ERROR, "UMI reset: PON write %04x=%02x %r\n", Address, Value, Status));
  return Status;
}

STATIC VOID
BoardReset (UINT8 Type)
{
  UINT8 Revision;
  UINT8 Subtype;
  UINT8 Control;
  UINT8 Enable;
  UINT8 Spare;
  UINT32 Mask;
  UINT32 PsHold;
  UINTN Result;
  EFI_STATUS Status;
  BOOLEAN Interrupts;
  BOOLEAN Sdi;
  BOOLEAN NativeDload;
  BOOLEAN Changed = FALSE;

  // No other CPUs are started by this menu firmware. Prevent the button
  // observer and touch callbacks from issuing SPMI during reset preparation.
  Interrupts = SaveAndDisableInterrupts ();
  Status = UmiSpmiRead8 (0, 0x0801, &Revision);
  if (!EFI_ERROR (Status)) Status = UmiSpmiRead8 (0, 0x0805, &Subtype);
  if (!EFI_ERROR (Status)) Status = UmiSpmiRead8 (0, 0x085A, &Control);
  if (!EFI_ERROR (Status)) Status = UmiSpmiRead8 (0, 0x085B, &Enable);
  if (!EFI_ERROR (Status)) Status = UmiSpmiRead8 (0, 0x088F, &Spare);
  if (EFI_ERROR (Status)) {
    goto Abort;
  }
  PsHold = MmioRead32 (0x0C264000);
  DEBUG ((DEBUG_ERROR, "UMI reset: rev=%02x subtype=%02x ctl=%02x en=%02x spare=%02x PS_HOLD=%08x type=%u\n",
          Revision, Subtype, Control, Enable, Spare, PsHold, Type));
  if ((Subtype != 4) || (PsHold != 1) ||
      ((Type != 1) && (Type != 4) && (Type != 7))) {
    Status = EFI_UNSUPPORTED;
    goto Abort;
  }
  // Match Linux's read-only SCM convention detection before PMIC mutation.
  if (ScmCall (0xC2000601, 1, 0x82000601, 0, &Result) && (Result != 0)) {
    Mask = 0x40000000;
  } else if (ScmCall (0x82000601, 1, 0x82000601, 0, &Result) && (Result != 0)) {
    Mask = 0;
  } else {
    Status = EFI_UNSUPPORTED;
    goto Abort;
  }
  // 902 stops the arbiter and deasserts PS_HOLD. Preflight the native dload
  // call or Linux's secure IO-write fallback before changing any PON state.
  if (!ScmAvailable (Mask, 0x02000902)) {
    Status = EFI_UNSUPPORTED;
    goto Abort;
  }
  NativeDload = ScmAvailable (Mask, 0x02000110);
  if (!NativeDload && !ScmAvailable (Mask, 0x02000502)) {
    Status = EFI_UNSUPPORTED;
    goto Abort;
  }
  Sdi = ScmAvailable (Mask, 0x02000109);
  Changed = TRUE;
  Status = WriteVerify (0x085B, Enable & ~BIT7);
  if (EFI_ERROR (Status)) goto Abort;
  MicroSecondDelay (500);
  Status = WriteVerify (0x085A, (Control & 0xF0) | Type);
  if (EFI_ERROR (Status)) goto Abort;
  Status = WriteVerify (0x085B, Enable | BIT7);
  if (EFI_ERROR (Status)) goto Abort;
  // This board explicitly stores Gen2 hard-reset reasons, including warm
  // restart targets. NORMAL=0x20; the field is bits 7:1.
  Status = WriteVerify (0x088F, (Spare & BIT0) | 0x40);
  if (EFI_ERROR (Status)) goto Abort;
  if (!ClearDload (Mask, NativeDload)) {
    Status = EFI_DEVICE_ERROR;
    goto Abort;
  }
  if (Sdi && !ScmCall (0x82000109 | Mask, 2, 1, 0, &Result)) {
    Status = EFI_DEVICE_ERROR;
    goto Abort;
  }
  MmioWrite32 (0x146BF65C, 0x77665501);
  MemoryFence ();
  DEBUG ((DEBUG_ERROR, "UMI reset: normal target recorded; deasserting PS_HOLD\n"));
  ScmCall (0x82000902 | Mask, 1, 0, 0, &Result);
  // Linux's direct MMIO fall-through if the supported secure call returns.
  // No SPMI access after this point: the arbiter may already be halted.
  MmioWrite32 (0x0C264000, 0);
  MicroSecondDelay (1000000);
  DEBUG ((DEBUG_ERROR, "UMI reset: PS_HOLD did not complete reset\n"));
  CpuDeadLoop ();

Abort:
  DEBUG ((DEBUG_ERROR, "UMI reset: aborted before PS_HOLD: %r\n", Status));
  if (Changed) {
    // Keep reset disabled while restoring its original type, then restore
    // enable. These registers do not change the hardware long-power gesture.
    Status = WriteVerify (0x085B, Enable & ~BIT7);
    if (!EFI_ERROR (Status)) {
      MicroSecondDelay (500);
      Status = WriteVerify (0x085A, Control);
    }
    if (!EFI_ERROR (Status)) {
      Status = WriteVerify (0x088F, Spare);
    }
    if (!EFI_ERROR (Status)) {
      Status = WriteVerify (0x085B, Enable);
    }
    // Never re-enable an unverified reset type. A failed disable also
    // prevents any subsequent type writes. The long-power path is separate.
    DEBUG ((DEBUG_ERROR, "UMI reset: rollback %r\n", Status));
  }
  SetInterruptState (Interrupts);
}

VOID EFIAPI ResetCold (VOID) { BoardReset (7); }
VOID EFIAPI ResetWarm (VOID) { BoardReset (1); }
VOID EFIAPI ResetShutdown (VOID) { BoardReset (4); }
VOID EFIAPI ResetPlatformSpecific (UINTN DataSize, VOID *ResetData) { ResetCold (); }

VOID EFIAPI
ResetSystem (EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus,
             UINTN DataSize, VOID *ResetData OPTIONAL)
{
  switch (ResetType) {
    case EfiResetWarm: ResetWarm (); break;
    case EfiResetCold:
    case EfiResetPlatformSpecific: ResetCold (); break;
    case EfiResetShutdown: ResetShutdown (); break;
    default: break;
  }
}
