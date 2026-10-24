/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only fault injection. Includes actual library, mocks only hardware/time. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#undef NULL
#include "UmiRpmh.c"

STATIC UINT8 Db[DB_SIZE];
STATIC UINT32 Rsc[0x10000 / 4];
STATIC UINT64 ClockNs;
STATIC BOOLEAN CpuIrqs;
STATIC UINT32 Pointer;
STATIC UINTN Writes;
STATIC UINTN Triggers;
STATIC UINTN Enables;
STATIC UINTN Disables;
STATIC BOOLEAN BusyCommand;
STATIC BOOLEAN Incomplete;
STATIC UINTN DroppedAddress;
STATIC UINT32 DroppedValue;
STATIC UINTN Drops;
STATIC UINT32 Resource;
STATIC BOOLEAN ExpireBeforeTrigger;
STATIC UINT32 RestoredControl;

STATIC UINT32 *
Reg (UINTN Address)
{
  assert(Address >= RSC_BASE && Address < RSC_BASE + sizeof(Rsc));
  assert((Address & 3) == 0);
  return &Rsc[(Address - RSC_BASE) / 4];
}

UINT16 EFIAPI
MmioRead16 (UINTN Address)
{
  UINT16 Value;
  assert(Address >= DB_BASE && Address <= DB_BASE + DB_SIZE - 2);
  assert((Address & 1) == 0);
  memcpy(&Value, Db + (Address - DB_BASE), sizeof(Value));
  return Value;
}

UINT32 EFIAPI
MmioRead32 (UINTN Address)
{
  UINT32 Value;
  if (Address == DB_POINTER) return Pointer;
  if (Address >= DB_BASE && Address <= DB_BASE + DB_SIZE - 4) {
    assert((Address & 3) == 0);
    memcpy(&Value, Db + (Address - DB_BASE), sizeof(Value));
    return Value;
  }
  return *Reg(Address);
}

STATIC VOID
CompleteCommand (VOID)
{
  *Reg(Register(mTcs, STATUS)) = BIT0;
  *Reg(Register(mTcs, CMD_STATUS)) = Incomplete ? BIT8 : CMD_COMPLETE;
  *Reg(TCS_BASE + IRQ_STATUS) |= 1U << mTcs;
}

UINT32 EFIAPI
MmioWrite32 (UINTN Address, UINT32 Value)
{
  UINT32 Old;
  UINTN Offset;
  assert(!CpuIrqs);
  assert(mSlotOwned && mTcs < 2);
  ++Writes;
  if (Drops && Address == DroppedAddress && Value == DroppedValue) {
    --Drops;
    return Value;
  }
  if (Address == TCS_BASE + IRQ_CLEAR) {
    assert(Value == (1U << mTcs));
    *Reg(TCS_BASE + IRQ_STATUS) &= ~Value;
    return Value;
  }
  if (Address == TCS_BASE + IRQ_ENABLE) {
    assert(((*Reg(Address) ^ Value) & ~(1U << mTcs)) == 0);
    *Reg(Address) = Value;
    return Value;
  }
  Offset = Address - Register(mTcs, 0);
  assert(Offset == CONTROL || Offset == CMD_WAIT || Offset == CMD_ENABLE ||
         Offset == CMD_MSGID || Offset == CMD_ADDRESS || Offset == CMD_DATA);
  Old = *Reg(Address);
  if (Offset == CONTROL) {
    // Hardware order: drop trigger in a separate write before mode disable.
    if ((Old & AMC_ENABLE) && !(Value & AMC_ENABLE)) {
      assert(!(Old & AMC_TRIGGER));
    }
    // Enabling and triggering must be distinct, ordered writes.
    if ((Value & AMC_TRIGGER) && !(Old & AMC_TRIGGER)) {
      assert(Old & AMC_ENABLE);
    }
  }
  *Reg(Address) = Value;
  if (ExpireBeforeTrigger && Offset == CMD_ENABLE && Value == 1) {
    ClockNs = TRANSACTION_NS;
  }
  if (Offset == CONTROL && (Value & AMC_TRIGGER) && !(Old & AMC_TRIGGER)) {
    ++Triggers;
    assert(Value & AMC_ENABLE);
    assert(*Reg(TCS_BASE + IRQ_ENABLE) & (1U << mTcs));
    assert(*Reg(Register(mTcs, CMD_ADDRESS)) == Resource + 4);
    assert(*Reg(Register(mTcs, CMD_MSGID)) == WRITE_RESPONSE);
    assert(*Reg(Register(mTcs, CMD_ENABLE)) == 1);
    assert(*Reg(Register(mTcs, CMD_WAIT)) == 1);
    Value = *Reg(Register(mTcs, CMD_DATA));
    assert(Value <= 1);
    if (Value) ++Enables; else ++Disables;
    if (BusyCommand) {
      *Reg(Register(mTcs, STATUS)) = 0;
      *Reg(Register(mTcs, CMD_STATUS)) = BIT8;
    } else {
      CompleteCommand();
    }
  }
  return *Reg(Address);
}

UINT64 EFIAPI GetPerformanceCounter (VOID) { return ClockNs; }
UINT64 EFIAPI GetTimeInNanoSecond (UINT64 Ticks) { return Ticks; }
UINTN EFIAPI MicroSecondDelay (UINTN Us) { ClockNs += Us * 1000ULL; return Us; }
VOID EFIAPI MemoryFence (VOID) { }
BOOLEAN EFIAPI SaveAndDisableInterrupts (VOID) {
  BOOLEAN Old = CpuIrqs; CpuIrqs = FALSE; return Old;
}
BOOLEAN EFIAPI SetInterruptState (BOOLEAN Enabled) { CpuIrqs = Enabled; return Enabled; }

STATIC VOID
Put16 (UINTN Offset, UINT16 Value)
{
  memcpy(Db + Offset, &Value, 2);
}

STATIC VOID
Put32 (UINTN Offset, UINT32 Value)
{
  memcpy(Db + Offset, &Value, 4);
}

STATIC VOID
ResetMock (VOID)
{
  UINTN Index;
  memset(Db, 0, sizeof(Db));
  memset(Rsc, 0, sizeof(Rsc));
  mInCall = mSlotOwned = mInFlight = mInFlightEnable = mVoteOutstanding = FALSE;
  mSaved = (UMI_TCS_SNAPSHOT){0}; mTcs = 0; mAddress = 0;
  ClockNs = Writes = Triggers = Enables = Disables = Drops = 0;
  BusyCommand = Incomplete = FALSE; CpuIrqs = TRUE;
  ExpireBeforeTrigger = FALSE;
  RestoredControl = AMC_ENABLE;
  Pointer = DB_BASE; Resource = 0x41230; /* Synthetic, never a handset address. */
  Put32(0, 1); Put32(4, 0x0c0330db);
  Put16(8, 4); Put16(10, 0); Put16(14, 1);
  memcpy(Db + DB_DATA, "ldoc1", 5); Put32(DB_DATA + 16, Resource);
  *Reg(RSC_BASE + 0xc) = (16U << 27) | (8U << 12);
  *Reg(TCS_BASE + IRQ_ENABLE) = BIT5;
  *Reg(TCS_BASE + IRQ_STATUS) = BIT5; /* Preserve unrelated pending IRQ. */
  for (Index = 0; Index < 2; Index++) {
    *Reg(Register(Index, STATUS)) = 1;
    *Reg(Register(Index, CONTROL)) = AMC_ENABLE | AMC_TRIGGER;
    *Reg(Register(Index, CMD_MSGID)) = 0x12340000U + (UINT32)Index;
    *Reg(Register(Index, CMD_ADDRESS)) = 0x53210U + (UINT32)Index;
    *Reg(Register(Index, CMD_DATA)) = 0xbeefU + (UINT32)Index;
  }
}

STATIC VOID
AssertRestored (VOID)
{
  assert(!mSlotOwned && !mInFlight);
  assert(*Reg(Register(0, CONTROL)) == RestoredControl);
  assert(*Reg(Register(0, CMD_ENABLE)) == 0);
  assert(*Reg(Register(0, CMD_WAIT)) == 0);
  assert(*Reg(Register(0, CMD_MSGID)) == 0x12340000U);
  assert(*Reg(Register(0, CMD_ADDRESS)) == 0x53210U);
  assert(*Reg(Register(0, CMD_DATA)) == 0xbeefU);
  assert(*Reg(TCS_BASE + IRQ_ENABLE) == BIT5);
  assert(*Reg(TCS_BASE + IRQ_STATUS) == BIT5);
  assert(CpuIrqs);
}

int main (void)
{
  UINTN Before;
  ResetMock();
  assert(UmiRpmhTouchEnable() == EFI_SUCCESS);
  assert(UmiRpmhTouchOutstanding() && Enables == 1 && Disables == 0);
  AssertRestored();
  assert(UmiRpmhTouchEnable() == EFI_ALREADY_STARTED);
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS);
  assert(!UmiRpmhTouchOutstanding() && Triggers == 2 && Disables == 1);
  AssertRestored();
  Before = Writes;
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && Writes == Before);

  // Actual attempt13 handoff: both active slots idle/empty, CONTROL=0.
  ResetMock(); RestoredControl = 0;
  *Reg(Register(0, CONTROL)) = 0;
  *Reg(Register(1, CONTROL)) = 0;
  assert(UmiRpmhTouchEnable() == EFI_SUCCESS && Enables == 1);
  AssertRestored();
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && Disables == 1);
  assert(!UmiRpmhTouchOutstanding()); AssertRestored();

  ResetMock();
  *Reg(Register(0, CONTROL)) |= BIT4;
  *Reg(Register(1, CONTROL)) |= BIT4;
  assert(UmiRpmhTouchEnable() == EFI_NOT_READY && !Writes);

  // A mode-enable readback failure must never issue a trigger.
  ResetMock(); RestoredControl = 0;
  *Reg(Register(0, CONTROL)) = 0;
  *Reg(Register(1, CONTROL)) = 0;
  DroppedAddress = Register(0, CONTROL); DroppedValue = AMC_ENABLE; Drops = 1;
  assert(UmiRpmhTouchEnable() == EFI_TIMEOUT && !Triggers && !mInFlight);
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && !UmiRpmhTouchOutstanding());
  AssertRestored();

  ResetMock(); Pointer = DB_BASE + 4;
  assert(UmiRpmhTouchEnable() == EFI_UNSUPPORTED && !Writes && CpuIrqs);
  ResetMock(); Put32(4, 0);
  assert(UmiRpmhTouchEnable() == EFI_UNSUPPORTED && !Writes);
  ResetMock(); Put16(10, 0xfffc); Put16(14, 0xffff);
  assert(UmiRpmhTouchEnable() == EFI_COMPROMISED_DATA && !Writes);
  ResetMock(); Put32(DB_DATA + 16, 0x51230);
  assert(UmiRpmhTouchEnable() == EFI_COMPROMISED_DATA && !Writes);
  ResetMock(); *Reg(Register(1, STATUS)) = 0;
  assert(UmiRpmhTouchEnable() == EFI_NOT_READY && !Writes);
  ResetMock(); *Reg(TCS_BASE + IRQ_STATUS) |= BIT1;
  assert(UmiRpmhTouchEnable() == EFI_NOT_READY && !Writes);

  ResetMock(); BusyCommand = TRUE;
  assert(UmiRpmhTouchEnable() == EFI_TIMEOUT);
  assert(mInFlight && mSlotOwned && UmiRpmhTouchOutstanding() && CpuIrqs);
  assert(*Reg(Register(0, CMD_ADDRESS)) == Resource + 4);
  assert(*Reg(Register(0, CMD_ENABLE)) == 1); /* Busy payload not overwritten. */
  assert((*Reg(TCS_BASE + IRQ_ENABLE) & BIT0) == 0); /* Late IRQ isolated. */
  assert(ClockNs <= TRANSACTION_NS + IRQ_MASK_NS);
  Before = Triggers;
  assert(UmiRpmhTouchDisable() == EFI_TIMEOUT && Triggers == Before);
  CompleteCommand(); BusyCommand = FALSE;
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && Disables == 1);
  assert(!UmiRpmhTouchOutstanding()); AssertRestored();

  ResetMock(); BusyCommand = TRUE;
  assert(UmiRpmhTouchEnable() == EFI_TIMEOUT);
  *Reg(Register(0, STATUS)) = 1;
  *Reg(Register(0, CMD_STATUS)) = CMD_COMPLETE; /* No fresh IRQ: insufficient. */
  assert(UmiRpmhTouchDisable() == EFI_TIMEOUT && Triggers == 1);
  CompleteCommand(); BusyCommand = FALSE;
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS); AssertRestored();

  ResetMock(); Incomplete = TRUE;
  assert(UmiRpmhTouchEnable() == EFI_DEVICE_ERROR && !mInFlight);
  assert(UmiRpmhTouchOutstanding() && Enables == 1 && !Disables);
  Incomplete = FALSE;
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS); AssertRestored();

  ResetMock();
  assert(UmiRpmhTouchEnable() == EFI_SUCCESS);
  Incomplete = TRUE;
  assert(UmiRpmhTouchDisable() == EFI_DEVICE_ERROR && !mInFlight);
  assert(UmiRpmhTouchOutstanding() && mVoteOutstanding && Disables == 1);
  Incomplete = FALSE;
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && Disables == 2);
  assert(!UmiRpmhTouchOutstanding()); AssertRestored();

  ResetMock();
  ExpireBeforeTrigger = TRUE;
  assert(UmiRpmhTouchEnable() == EFI_TIMEOUT && !Triggers);
  assert(!mInFlight && !mVoteOutstanding && mSlotOwned);
  ExpireBeforeTrigger = FALSE;
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && !UmiRpmhTouchOutstanding());
  assert(!Triggers); AssertRestored();

  ResetMock();
  DroppedAddress = Register(0, CMD_MSGID); DroppedValue = WRITE_RESPONSE; Drops = 1;
  assert(UmiRpmhTouchEnable() == EFI_TIMEOUT && !Triggers && mSlotOwned);
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && !UmiRpmhTouchOutstanding());
  AssertRestored();

  ResetMock();
  DroppedAddress = Register(0, CMD_MSGID); DroppedValue = 0x12340000; Drops = 1;
  assert(UmiRpmhTouchEnable() == EFI_TIMEOUT && Enables == 1);
  assert(UmiRpmhTouchOutstanding() && !mInFlight && mSlotOwned);
  assert(UmiRpmhTouchDisable() == EFI_SUCCESS && Disables == 1);
  AssertRestored();
  puts("RPMh regression: PASS (guards, write bounds, trigger non-replay, IRQ preservation, timeout/inflight, cleanup retry)");
  return 0;
}
