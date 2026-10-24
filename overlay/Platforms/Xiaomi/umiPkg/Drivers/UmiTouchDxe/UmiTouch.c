/** @file
  Xiaomi Mi 10 (umi) ST FTS: dedicated GPIO open-drain I2C, without GENI/GSI
  or clock initialization; guarded touch power enable only. See SOURCES.txt.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/DevicePath.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UmiSpmiLib.h>
#include <Library/UmiRpmhLib.h>
#include "FtsEvent.h"

#define SE13_BASE          0x00A94000U
#define TLMM_SOUTH         0x0F500000U
#define GPIO_CTL(N)        (TLMM_SOUTH + (N) * 0x1000U)
#define GPIO_IO(N)         (GPIO_CTL (N) + 4U)
#define SDA                36U
#define SCL                37U
#define GPIO_FUNC_MASK     0x3CU
#define GPIO_PULL_MASK     0x3U
#define GPIO_DRIVE_MASK    0x1C0U
#define GPIO_OE            BIT9
#define GPIO_OUT           BIT1
#define GPIO_IN            BIT0
#define GENI_BUSY_MASK     (BIT0 | BIT12)
#define HALF_CYCLE_US      5U
#define STRETCH_LIMIT_US   500U
#define TRANSFER_LIMIT_NS  8000000ULL
#define POLL_INTERVAL      200000ULL /* 20 ms, in UEFI 100 ns units. */
#define MAX_RECORDS        4U

STATIC EFI_HANDLE mHandle;
STATIC EFI_EVENT  mTimer;
STATIC EFI_EVENT  mExitEvent;
STATIC BOOLEAN   mOwned;
STATIC BOOLEAN   mStopped;
STATIC BOOLEAN   mFailed;
STATIC BOOLEAN   mVerified;
STATIC BOOLEAN   mScanPending;
STATIC BOOLEAN   mCancelPending;
STATIC UINT32    mSavedCtl[2];
STATIC UINT32    mSavedOut[2];
STATIC UINT32    mGpioCtl[2];
STATIC UINT64    mTransferStart;
STATIC UINT32    mAckMask;
STATIC UINT32    mAckIndex;
STATIC UINT32    mTraceCount;
STATIC UMI_FTS_STATE mFts;
STATIC BOOLEAN mPowerOwned;
STATIC BOOLEAN mAwaitReady;
STATIC UINT8 mSupplySid;
STATIC UINT8 mSupplyEnable;
STATIC UINT32 mPowerCtl[2];
STATIC UINT32 mPowerOut[2];
STATIC UINT64 mReadyStart;

STATIC EFI_ABSOLUTE_POINTER_MODE mMode = {0, 0, 0, 1079, 2339, 0, 0};
typedef struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} UMI_TOUCH_DEVICE_PATH;
STATIC UMI_TOUCH_DEVICE_PATH mPath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   {0xBB56C123, 0xEB0E, 0x41F0, {0x8E, 0xE4, 0x33, 0x1A, 0x20, 0xAE, 0x7F, 0xCE}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {4, 0}}
};

STATIC VOID
Snapshot (VOID)
{
  STATIC CONST UINT32 Pins[] = {36, 37, 38, 39, 69};
  STATIC CONST UINT32 Offsets[] = {0x40, 0x48, 0x64, 0x68, 0x7C, 0x908};
  UINTN Index;
  for (Index = 0; Index < ARRAY_SIZE (Offsets); Index++) {
    DEBUG ((DEBUG_WARN, "UMI touch SE13+%03x=%08x\n", Offsets[Index],
            MmioRead32 (SE13_BASE + Offsets[Index])));
  }
  for (Index = 0; Index < ARRAY_SIZE (Pins); Index++) {
    DEBUG ((DEBUG_WARN, "UMI touch GPIO%u ctl=%08x io=%08x\n", Pins[Index],
            MmioRead32 (GPIO_CTL (Pins[Index])), MmioRead32 (GPIO_IO (Pins[Index]))));
  }
}

STATIC BOOLEAN
High (UINT32 Pin)
{
  return (MmioRead32 (GPIO_IO (Pin)) & GPIO_IN) != 0;
}

STATIC BOOLEAN
PowerReady (VOID)
{
  UINT32 Pin;
  UINT32 Ctl;
  UINT32 Io;
  /* Digital state only; this is not a rail voltage measurement. */
  for (Pin = 38; Pin <= 69; Pin += 31) {
    Ctl = MmioRead32 (GPIO_CTL (Pin));
    Io = MmioRead32 (GPIO_IO (Pin));
    if (((Ctl & GPIO_FUNC_MASK) != 0) || ((Ctl & GPIO_OE) == 0) ||
        ((Io & (GPIO_OUT | GPIO_IN)) != (GPIO_OUT | GPIO_IN))) {
      return FALSE;
    }
  }
  return TRUE;
}

STATIC VOID
RestorePower (VOID)
{
  UINTN Index;
  UINT32 Pin;
  EFI_STATUS Status;
  if (mPowerOwned) {
    // Restore reset first, then AVDD. Original pins are recorded before writes.
    for (Index = 0; Index < 2; Index++) {
      Pin = (Index == 0) ? 38 : 69;
      MmioWrite32 (GPIO_CTL (Pin), MmioRead32 (GPIO_CTL (Pin)) & ~GPIO_OE);
      MmioWrite32 (GPIO_IO (Pin), (MmioRead32 (GPIO_IO (Pin)) & ~GPIO_OUT) | mPowerOut[Index]);
      MmioWrite32 (GPIO_CTL (Pin), mPowerCtl[Index]);
    }
    mPowerOwned = FALSE;
  }
  if (UmiRpmhTouchOutstanding ()) {
    Status = UmiRpmhTouchDisable ();
    DEBUG ((DEBUG_WARN, "UMI touch withdraw RPMh enable status=%r outstanding=%u\n",
            Status, UmiRpmhTouchOutstanding ()));
    (VOID)Status;
  }
}

STATIC VOID
DrivePowerPin (UINT32 Pin, BOOLEAN HighValue)
{
  UINT32 Ctl = MmioRead32 (GPIO_CTL (Pin));
  Ctl &= ~(GPIO_FUNC_MASK | GPIO_PULL_MASK | GPIO_DRIVE_MASK | GPIO_OE);
  if ((Ctl & BIT11) != 0) {
    Ctl |= BIT12;
  }
  MmioWrite32 (GPIO_CTL (Pin), Ctl);
  MmioWrite32 (GPIO_IO (Pin),
               (MmioRead32 (GPIO_IO (Pin)) & ~GPIO_OUT) | (HighValue ? GPIO_OUT : 0));
  MmioWrite32 (GPIO_CTL (Pin), Ctl | GPIO_OE);
  MemoryFence ();
}

STATIC EFI_STATUS
InitializePower (VOID)
{
  STATIC CONST UINT8 Sids[] = {0, 2, 4, 8, 10};
  STATIC CONST UINT16 Registers[] = {0x4004, 0x4005, 0x4008, 0x4040, 0x4041,
                                    0x4042, 0x4043, 0x4045, 0x4046};
  UINT8 Type;
  UINT8 Model;
  UINT8 Values[ARRAY_SIZE (Registers)];
  UINT8 Ready = 0;
  UINT8 Enable = 0;
  UINTN Index;
  UINTN Matches = 0;
  EFI_STATUS Status;

  // RPMh "ldoc1" names PM8150A L1. Find the unique physical PM8150A rather
  // than trusting a historical DT node name. The original PmicDxe reads
  // type/model at 104/105 and enumerates primary/secondary SID pairs.
  for (Index = 0; Index < ARRAY_SIZE (Sids); Index++) {
    Type = Model = 0;
    Status = UmiSpmiRead8 (Sids[Index], 0x0104, &Type);
    if (!EFI_ERROR (Status)) {
      Status = UmiSpmiRead8 (Sids[Index], 0x0105, &Model);
    }
    DEBUG ((DEBUG_WARN, "UMI touch PMIC SID%u type=%02x model=%02x status=%r\n",
            Sids[Index], Type, Model, Status));
    if (!EFI_ERROR (Status) && (Type == 0x51) && (Model == 0x27)) {
      Matches++;
      mSupplySid = Sids[Index] + 1;
    }
  }
  if (Matches != 1) {
    return EFI_UNSUPPORTED;
  }
  for (Index = 0; Index < ARRAY_SIZE (Registers); Index++) {
    Values[Index] = 0;
    Status = UmiSpmiRead8 (mSupplySid, Registers[Index], &Values[Index]);
    DEBUG ((DEBUG_WARN, "UMI touch L1 SID%u reg=%04x value=%02x status=%r\n",
            mSupplySid, Registers[Index], Values[Index], Status));
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  // This handset read type04/subtype71 on attempt12. Reuse only that observed
  // peripheral and an existing 1800mV setting; no voltage/mode programming.
  if ((Values[0] != 4) || (Values[1] != 0x71) ||
      (((UINT16)Values[4] << 8) | Values[3]) != 1800) {
    return EFI_UNSUPPORTED;
  }
  mSupplyEnable = Values[8];
  if ((mSupplyEnable & BIT7) == 0) {
    // The observed aggregate software enable must be exactly zero. Leave
    // pin-controlled or otherwise unfamiliar enable values untouched.
    if ((mSupplyEnable != 0) || ((Values[2] & BIT7) != 0)) {
      return EFI_UNSUPPORTED;
    }
    // This requests ldoc1 enable through APPS DRV2. Cleanup sends data0 for
    // our request; it cannot reconstruct an unknown inherited DRV vote.
    Status = UmiRpmhTouchEnable ();
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  Status = UmiSpmiRead8 (mSupplySid, 0x4046, &Enable);
  DEBUG ((DEBUG_WARN, "UMI touch L1 after vote enable=%02x status=%r\n", Enable, Status));
  if (EFI_ERROR (Status) || ((Enable & BIT7) == 0)) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  for (Index = 0; Index < 100; Index++) {
    Status = UmiSpmiRead8 (mSupplySid, 0x4008, &Ready);
    if (EFI_ERROR (Status) || ((Ready & BIT7) != 0)) {
      break;
    }
    MicroSecondDelay (1000);
  }
  if (Index == 100) {
    Status = EFI_TIMEOUT;
  }
  DEBUG ((DEBUG_WARN, "UMI touch L1 ready=%02x polls=%u status=%r\n", Ready,
          (UINT32)((Index < 100) ? Index + 1 : 100), Status));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Values[5] = Values[6] = 0;
  Status = UmiSpmiRead8 (mSupplySid, 0x4042, &Values[5]);
  if (!EFI_ERROR (Status)) {
    Status = UmiSpmiRead8 (mSupplySid, 0x4043, &Values[6]);
  }
  DEBUG ((DEBUG_WARN, "UMI touch L1 voltage readback=%02x/%02x mV=%u status=%r\n",
          Values[5], Values[6], (UINT32)(((UINT16)Values[6] << 8) | Values[5]), Status));
  if (EFI_ERROR (Status) || ((((UINT16)Values[6] << 8) | Values[5]) != 1800)) {
    return EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
  }
  MicroSecondDelay (15000); // fts_enable_reg: VDD, 15 ms, then AVDD.
  for (Index = 0; Index < 2; Index++) {
    UINT32 Pin = (Index == 0) ? 38 : 69;
    mPowerCtl[Index] = MmioRead32 (GPIO_CTL (Pin));
    mPowerOut[Index] = MmioRead32 (GPIO_IO (Pin)) & GPIO_OUT;
  }
  mPowerOwned = TRUE;
  DrivePowerPin (38, FALSE);
  DrivePowerPin (69, TRUE);
  MicroSecondDelay (10000); // fts_system_reset: RESET low for 10 ms.
  DrivePowerPin (38, TRUE);
  mAwaitReady = TRUE;
  mReadyStart = GetPerformanceCounter ();
  DEBUG ((DEBUG_WARN, "UMI touch power: L1 SID%u ready at 1800mV; AVDD high; reset released\n", mSupplySid));
  return PowerReady () ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC VOID
ReleasePins (VOID)
{
  UINTN Index;
  UINT32 Pin;
  if (!mOwned) {
    return;
  }
  /* First release both drivers, then restore the saved output latch and mux.
   * Only restore writable OUT bit 1, never replay sampled input/status bits. */
  for (Index = 0; Index < 2; Index++) {
    MmioWrite32 (GPIO_CTL (SDA + (UINT32)Index), mGpioCtl[Index] & ~GPIO_OE);
  }
  MemoryFence ();
  for (Index = 0; Index < 2; Index++) {
    Pin = SDA + (UINT32)Index;
    MmioWrite32 (GPIO_IO (Pin), (MmioRead32 (GPIO_IO (Pin)) & ~GPIO_OUT) | mSavedOut[Index]);
    MmioWrite32 (GPIO_CTL (Pin), mSavedCtl[Index]);
  }
  MemoryFence ();
  mOwned = FALSE;
}

STATIC EFI_STATUS
AcquirePins (VOID)
{
  UINTN Index;
  UINT32 Pin;
  UINT32 Ctl;
  if ((MmioRead32 (SE13_BASE + 0x40) & GENI_BUSY_MASK) != 0) {
    DEBUG ((DEBUG_WARN, "UMI touch preflight: GENI active\n"));
    return EFI_ACCESS_DENIED;
  }
  if (!PowerReady ()) {
    DEBUG ((DEBUG_WARN, "UMI touch preflight: initializing touch power/reset\n"));
    EFI_STATUS Status = InitializePower ();
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }
  for (Index = 0; Index < 2; Index++) {
    Pin = SDA + (UINT32)Index;
    mSavedCtl[Index] = MmioRead32 (GPIO_CTL (Pin));
    mSavedOut[Index] = MmioRead32 (GPIO_IO (Pin)) & GPIO_OUT;
    Ctl = mSavedCtl[Index] & ~(GPIO_FUNC_MASK | GPIO_OE | GPIO_PULL_MASK | GPIO_DRIVE_MASK);
    if ((Ctl & BIT11) != 0) {
      Ctl |= BIT12; /* Linux pinctrl-msm: enable eGPIO if present. */
    }
    mGpioCtl[Index] = Ctl;
  }
  mOwned = TRUE;
  for (Index = 0; Index < 2; Index++) {
    Pin = SDA + (UINT32)Index;
    MmioWrite32 (GPIO_CTL (Pin), mGpioCtl[Index]); /* GPIO input: released. */
    MmioWrite32 (GPIO_IO (Pin), MmioRead32 (GPIO_IO (Pin)) & ~GPIO_OUT);
    if (MmioRead32 (GPIO_CTL (Pin)) != mGpioCtl[Index]) {
      DEBUG ((DEBUG_WARN, "UMI touch preflight: GPIO%u configuration readback failed\n", Pin));
      ReleasePins ();
      return EFI_WRITE_PROTECTED;
    }
  }
  MemoryFence ();
  MicroSecondDelay (HALF_CYCLE_US);
  if (!High (SDA) || !High (SCL)) {
    DEBUG ((DEBUG_WARN, "UMI touch preflight: released SDA=%u SCL=%u\n", High (SDA), High (SCL)));
    ReleasePins ();
    return EFI_NOT_READY; /* No recovery clocks, pull-up changes or power cycle. */
  }
  return EFI_SUCCESS;
}

STATIC VOID
Line (UINT32 Pin, BOOLEAN Released)
{
  UINTN Index = Pin - SDA;
  /* Output latch stays zero for the complete ownership interval. */
  MmioWrite32 (GPIO_CTL (Pin), mGpioCtl[Index] | (Released ? 0 : GPIO_OE));
  MemoryFence ();
}

STATIC BOOLEAN
Expired (VOID)
{
  return GetTimeInNanoSecond (GetPerformanceCounter () - mTransferStart) >= TRANSFER_LIMIT_NS;
}

STATIC EFI_STATUS
ClockHigh (VOID)
{
  UINT32 Wait;
  if (Expired ()) {
    return EFI_TIMEOUT;
  }
  Line (SCL, TRUE);
  for (Wait = 0; !High (SCL); Wait++) {
    if ((Wait >= STRETCH_LIMIT_US) || Expired ()) {
      return EFI_TIMEOUT;
    }
    MicroSecondDelay (1);
  }
  MicroSecondDelay (HALF_CYCLE_US);
  return EFI_SUCCESS;
}

STATIC VOID
ClockLow (VOID)
{
  Line (SCL, FALSE);
  MicroSecondDelay (HALF_CYCLE_US);
}

STATIC EFI_STATUS
Start (BOOLEAN Repeated)
{
  EFI_STATUS Status;
  Line (SDA, TRUE);
  MicroSecondDelay (HALF_CYCLE_US);
  Status = ClockHigh ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (!High (SDA)) {
    return Repeated ? EFI_PROTOCOL_ERROR : EFI_NOT_READY;
  }
  Line (SDA, FALSE);
  MicroSecondDelay (HALF_CYCLE_US);
  ClockLow ();
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
Stop (VOID)
{
  EFI_STATUS Status;
  ClockLow ();
  Line (SDA, FALSE);
  MicroSecondDelay (HALF_CYCLE_US);
  Status = ClockHigh ();
  Line (SDA, TRUE);
  MicroSecondDelay (HALF_CYCLE_US);
  return Status;
}

STATIC EFI_STATUS
WriteByte (UINT8 Value)
{
  INTN Bit;
  EFI_STATUS Status;
  BOOLEAN Ack;
  for (Bit = 7; Bit >= 0; Bit--) {
    Line (SDA, ((Value >> Bit) & 1) != 0);
    MicroSecondDelay (HALF_CYCLE_US);
    Status = ClockHigh ();
    if (EFI_ERROR (Status)) {
      return Status;
    }
    if (((Value >> Bit) & 1) && !High (SDA)) {
      return EFI_PROTOCOL_ERROR; /* Released SDA unexpectedly held low. */
    }
    ClockLow ();
  }
  Line (SDA, TRUE);
  MicroSecondDelay (HALF_CYCLE_US);
  Status = ClockHigh ();
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Ack = !High (SDA);
  if (Ack && (mAckIndex < 32)) {
    mAckMask |= 1U << mAckIndex;
  }
  mAckIndex++;
  ClockLow ();
  return Ack ? EFI_SUCCESS : EFI_NO_RESPONSE;
}

STATIC EFI_STATUS
ReadByte (UINT8 *Value, BOOLEAN Last)
{
  UINTN Bit;
  EFI_STATUS Status;
  *Value = 0;
  Line (SDA, TRUE);
  for (Bit = 0; Bit < 8; Bit++) {
    Status = ClockHigh ();
    if (EFI_ERROR (Status)) {
      return Status;
    }
    *Value = (UINT8)((*Value << 1) | (High (SDA) ? 1 : 0));
    ClockLow ();
  }
  Line (SDA, Last); /* ACK every byte except the final byte (NACK). */
  MicroSecondDelay (HALF_CYCLE_US);
  Status = ClockHigh ();
  ClockLow ();
  Line (SDA, TRUE);
  return Status;
}

STATIC EFI_STATUS
Transfer (CONST UINT8 *Write, UINTN WriteSize, UINT8 *Read, UINTN ReadSize)
{
  EFI_STATUS Status;
  EFI_STATUS StopStatus;
  UINTN Index;
  if (!mOwned || !PowerReady () ||
      ((MmioRead32 (SE13_BASE + 0x40) & GENI_BUSY_MASK) != 0)) {
    return EFI_ACCESS_DENIED;
  }
  mTransferStart = GetPerformanceCounter ();
  mAckMask = 0;
  mAckIndex = 0;
  Status = Start (FALSE);
  if (!EFI_ERROR (Status)) {
    Status = WriteByte (0x92); /* The only slave address used: 0x49, write. */
  }
  for (Index = 0; !EFI_ERROR (Status) && (Index < WriteSize); Index++) {
    Status = WriteByte (Write[Index]);
  }
  if (!EFI_ERROR (Status) && (ReadSize != 0)) {
    Status = Start (TRUE);
    if (!EFI_ERROR (Status)) {
      Status = WriteByte (0x93);
    }
    for (Index = 0; !EFI_ERROR (Status) && (Index < ReadSize); Index++) {
      Status = ReadByte (&Read[Index], Index + 1 == ReadSize);
    }
  }
  /* Even an expired transfer gets only one bounded STOP attempt. */
  mTransferStart = GetPerformanceCounter ();
  StopStatus = Stop ();
  if (!EFI_ERROR (Status)) {
    Status = StopStatus;
  }
  return Status;
}

STATIC VOID
Fail (EFI_STATUS Status, CONST CHAR8 *Phase)
{
  DEBUG ((DEBUG_WARN, "UMI touch disabled phase=%a status=%r ACK=%x count=%u\n",
          Phase, Status, mAckMask, mAckIndex));
  /* A transport error is cancellation, never a successful synthetic tap-up. */
  UmiFtsCancel (&mFts);
  mCancelPending = TRUE;
  mFailed = TRUE;
  if (mTimer != NULL) {
    gBS->SetTimer (mTimer, TimerCancel, 0);
  }
  ReleasePins ();
  RestorePower ();
}

STATIC EFI_STATUS
Consume (CONST UINT8 *Data, UINTN Count)
{
  UINTN Index;
  UINT32 Result;
  for (Index = 0; Index < Count; Index++) {
    if (mAwaitReady && (Data[Index * 8] != 0x03) && (Data[Index * 8] != 0xF3)) {
      continue;
    }
    Result = UmiFtsParse (&mFts, &Data[Index * 8]);
    if ((Result & UMI_FTS_READY) != 0) {
      /* Tell the consumer to cancel before exposing any post-reset samples.
       * A ready record itself is recoverable by normal volatile scanning. */
      mCancelPending = TRUE;
      mFts.Cancelled = 0;
      mAwaitReady = FALSE;
      mScanPending = TRUE;
    } else if (((Result & (UMI_FTS_ERROR | UMI_FTS_INVALID)) != 0) || mFts.Cancelled) {
      return EFI_COMPROMISED_DATA;
    }
  }
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI
Poll (EFI_EVENT Event, VOID *Context)
{
  EFI_STATUS Status;
  UINT8 Data[MAX_RECORDS * 8];
  UINT8 Command = 0x86;
  STATIC CONST UINT8 Scan[] = {0xA0, 0x00, 0x01};
  UINTN Remaining;
  (VOID)Event;
  (VOID)Context;
  if (mStopped || mFailed) {
    return;
  }
  if (!mOwned) {
    Status = AcquirePins ();
    if (EFI_ERROR (Status)) {
      Fail (Status, "preflight");
      return;
    }
  }
  Status = Transfer (&Command, 1, Data, 8);
  if (EFI_ERROR (Status)) {
    if (mAwaitReady && (Status == EFI_NO_RESPONSE) &&
        (GetTimeInNanoSecond (GetPerformanceCounter () - mReadyStart) < 100000000ULL)) {
      return; // A resetting chip can NACK; retry only within its boot window.
    }
    Fail (Status, "FIFO");
    return;
  }
  if (!mVerified || ((Data[0] != 0) && (mTraceCount < 8))) {
    DEBUG ((DEBUG_WARN, "UMI touch FIFO ACK=%x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            mAckMask, Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7]));
    mTraceCount++;
  }
  Status = Consume (Data, 1);
  if (EFI_ERROR (Status)) {
    Fail (Status, "record");
    return;
  }
  Remaining = Data[7] & 0x1F;
  if (Data[0] == 0) {
    Remaining = 0;
  }
  if (Remaining > MAX_RECORDS - 1) {
    Remaining = MAX_RECORDS - 1;
  }
  if (Remaining != 0) {
    Status = Transfer (&Command, 1, Data, Remaining * 8);
    if (!EFI_ERROR (Status)) {
      Status = Consume (Data, Remaining);
    }
    if (EFI_ERROR (Status)) {
      Fail (Status, "drain");
      return;
    }
  }
  if (mAwaitReady) {
    if (GetTimeInNanoSecond (GetPerformanceCounter () - mReadyStart) >= 100000000ULL) {
      Fail (EFI_TIMEOUT, "ready");
    }
    return;
  }
  // READY may have arrived in the drained records. Commit verification only
  // after the complete batch, before issuing its one pending scan command.
  if (!mVerified) {
    mVerified = TRUE;
    mScanPending = TRUE;
  }
  if (mScanPending) {
    /* Only normal volatile scanning, after a valid ACKed FIFO response.
     * No reset, firmware, flash save, calibration, power or recovery command. */
    Status = Transfer (Scan, sizeof (Scan), NULL, 0);
    DEBUG ((DEBUG_WARN, "UMI touch scan A0 00 01 status=%r ACK=%x\n", Status, mAckMask));
    if (EFI_ERROR (Status)) {
      Fail (Status, "scan");
      return;
    }
    mScanPending = FALSE;
  }
}

STATIC EFI_STATUS EFIAPI
Reset (EFI_ABSOLUTE_POINTER_PROTOCOL *This, BOOLEAN ExtendedVerification)
{
  EFI_TPL OldTpl;
  (VOID)This;
  (VOID)ExtendedVerification;
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  mFts.Head = 0;
  mFts.Count = 0;
  mFts.Point.Active = 0;
  mFts.Cancelled = 0;
  mCancelPending = TRUE;
  gBS->RestoreTPL (OldTpl);
  return mFailed ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI
GetState (EFI_ABSOLUTE_POINTER_PROTOCOL *This, EFI_ABSOLUTE_POINTER_STATE *State)
{
  EFI_TPL OldTpl;
  UMI_FTS_POINT Point;
  UINT32 Available;
  (VOID)This;
  if (State == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (mCancelPending || mFailed) {
    mCancelPending = FALSE;
    gBS->RestoreTPL (OldTpl);
    /* Consumer must clear the gesture/armed target without confirming it. */
    return EFI_DEVICE_ERROR;
  }
  Available = UmiFtsPop (&mFts, &Point);
  gBS->RestoreTPL (OldTpl);
  if (!Available) {
    return mFailed ? EFI_DEVICE_ERROR : EFI_NOT_READY;
  }
  State->CurrentX = Point.X;
  State->CurrentY = Point.Y;
  State->CurrentZ = 0;
  State->ActiveButtons = Point.Active ? EFI_ABSP_TouchActive : 0;
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI
WaitForInput (EFI_EVENT Event, VOID *Context)
{
  (VOID)Context;
  /* No hardware I/O at TPL_NOTIFY. Sampling runs only in Poll/TPL_CALLBACK. */
  if (mCancelPending || (mFts.Count != 0)) {
    gBS->SignalEvent (Event);
  }
}

STATIC EFI_ABSOLUTE_POINTER_PROTOCOL mPointer = {Reset, GetState, NULL, &mMode};

STATIC VOID EFIAPI
ExitBootServices (EFI_EVENT Event, VOID *Context)
{
  (VOID)Event;
  (VOID)Context;
  mStopped = TRUE;
  if (mTimer != NULL) {
    gBS->SetTimer (mTimer, TimerCancel, 0);
  }
  ReleasePins ();
  RestorePower ();
}

EFI_STATUS EFIAPI
UmiTouchUnload (EFI_HANDLE ImageHandle)
{
  EFI_STATUS Status;
  (VOID)ImageHandle;
  mStopped = TRUE;
  if (mTimer != NULL) {
    gBS->SetTimer (mTimer, TimerCancel, 0);
    gBS->CloseEvent (mTimer);
    mTimer = NULL;
  }
  ReleasePins ();
  RestorePower ();
  if (mExitEvent != NULL) {
    gBS->CloseEvent (mExitEvent);
    mExitEvent = NULL;
  }
  Status = EFI_SUCCESS;
  if (mHandle != NULL) {
    Status = gBS->UninstallMultipleProtocolInterfaces (mHandle,
               &gEfiAbsolutePointerProtocolGuid, &mPointer,
               &gEfiDevicePathProtocolGuid, &mPath, NULL);
  }
  if (!EFI_ERROR (Status) && (mPointer.WaitForInput != NULL)) {
    gBS->CloseEvent (mPointer.WaitForInput);
    mPointer.WaitForInput = NULL;
    mHandle = NULL;
  }
  return Status;
}

EFI_STATUS EFIAPI
UmiTouchEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  (VOID)ImageHandle;
  (VOID)SystemTable;
  Snapshot ();
  Status = gBS->CreateEvent (EVT_NOTIFY_WAIT, TPL_NOTIFY, WaitForInput, NULL,
                             &mPointer.WaitForInput);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                             Poll, NULL, &mTimer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, ExitBootServices,
                               NULL, &gEfiEventExitBootServicesGuid, &mExitEvent);
  }
  if (!EFI_ERROR (Status)) {
    Status = gBS->InstallMultipleProtocolInterfaces (&mHandle,
               &gEfiAbsolutePointerProtocolGuid, &mPointer,
               &gEfiDevicePathProtocolGuid, &mPath, NULL);
  }
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mTimer, TimerPeriodic, POLL_INTERVAL);
  }
  if (EFI_ERROR (Status)) {
    UmiTouchUnload (ImageHandle);
  }
  return Status;
}
