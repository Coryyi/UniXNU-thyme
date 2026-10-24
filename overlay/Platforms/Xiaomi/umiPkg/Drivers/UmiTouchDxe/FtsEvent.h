/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* New, host-testable decoder of the ST FTM5 wire format documented in
 * LineageOS android_kernel_xiaomi_sm8250 at 71b13e62f057a649b77fe4062feb73ee72ad609c,
 * drivers/input/touchscreen/fts_521/{fts.c,fts_lib/ftsSoftware.h} (GPL-2.0).
 * This file does not contain the Linux driver implementation.
 */
#ifndef UMI_FTS_EVENT_H
#define UMI_FTS_EVENT_H

#define UMI_FTS_QUEUE_SIZE 32U
#define UMI_FTS_IGNORED    0U
#define UMI_FTS_CHANGED    1U
#define UMI_FTS_READY      2U
#define UMI_FTS_ERROR      4U
#define UMI_FTS_INVALID    8U

typedef struct {
  unsigned short X;
  unsigned short Y;
  unsigned char  Id;
  unsigned char  Active;
} UMI_FTS_POINT;

typedef struct {
  UMI_FTS_POINT Point;
  UMI_FTS_POINT Queue[UMI_FTS_QUEUE_SIZE];
  unsigned int  Head;
  unsigned int  Count;
  unsigned int  Dropped;
  unsigned int  Cancelled;
} UMI_FTS_STATE;

static inline void
UmiFtsCancel (UMI_FTS_STATE *State)
{
  State->Head = 0;
  State->Count = 0;
  State->Point.Active = 0;
  State->Cancelled = 1;
}

static inline void
UmiFtsPush (UMI_FTS_STATE *State)
{
  unsigned int Tail;
  if (State->Count == UMI_FTS_QUEUE_SIZE) {
    State->Dropped++;
    UmiFtsCancel (State); /* Lost ordering must never become a normal tap-up. */
    return;
  }
  Tail = (State->Head + State->Count) % UMI_FTS_QUEUE_SIZE;
  State->Queue[Tail] = State->Point;
  State->Count++;
}

static inline unsigned int
UmiFtsPop (UMI_FTS_STATE *State, UMI_FTS_POINT *Point)
{
  if (State->Count == 0U) {
    return 0U;
  }
  *Point = State->Queue[State->Head];
  State->Head = (State->Head + 1U) % UMI_FTS_QUEUE_SIZE;
  State->Count--;
  return 1U;
}

static inline void
UmiFtsRelease (UMI_FTS_STATE *State)
{
  if (State->Point.Active) {
    State->Point.Active = 0;
    /* A leave record's coordinates are not valid: preserve the last XY. */
    UmiFtsPush (State);
  }
}

static inline unsigned int
UmiFtsParse (UMI_FTS_STATE *State, const unsigned char Event[8])
{
  unsigned int Id;
  unsigned int Type;
  unsigned int X;
  unsigned int Y;
  UMI_FTS_POINT Previous;

  if (State->Cancelled) {
    return UMI_FTS_INVALID;
  }
  if (Event[0] == 0x00U) {
    return UMI_FTS_IGNORED;
  }
  if (Event[0] == 0x03U) {
    UmiFtsCancel (State);
    return UMI_FTS_READY;
  }
  if (Event[0] == 0xF3U) {
    UmiFtsCancel (State);
    return UMI_FTS_ERROR;
  }
  if ((Event[0] & 0x0FU) != 0x03U) {
    UmiFtsCancel (State);
    return UMI_FTS_INVALID;
  }
  if ((Event[0] != 0x13U) && (Event[0] != 0x23U) && (Event[0] != 0x33U)) {
    return UMI_FTS_IGNORED; /* Status/user-report records are not touches. */
  }
  Id = Event[1] >> 4;
  if (Id >= 10U) {
    UmiFtsCancel (State);
    return UMI_FTS_INVALID;
  }
  if (Event[0] == 0x33U) {
    if (State->Point.Active && (Id == State->Point.Id)) {
      UmiFtsRelease (State);
      return UMI_FTS_CHANGED;
    }
    if (State->Point.Active) {
      UmiFtsCancel (State);
      return UMI_FTS_INVALID;
    }
    return UMI_FTS_IGNORED;
  }
  Type = Event[1] & 0x0FU;
  if ((Type != 1U) && (Type != 2U)) {
    if (State->Point.Active) {
      UmiFtsCancel (State);
      return UMI_FTS_INVALID;
    }
    return UMI_FTS_IGNORED; /* Finger/glove only; no palm, hover or pen. */
  }
  if (State->Point.Active && (Id != State->Point.Id)) {
    UmiFtsCancel (State); /* Conservative: stop this boot on multiple contacts. */
    return UMI_FTS_INVALID;
  }
  X = ((unsigned int)(Event[3] & 0x0FU) << 8) | Event[2];
  Y = ((unsigned int)Event[4] << 4) | (Event[3] >> 4);
  if ((X > 1079U) || (Y > 2339U)) {
    UmiFtsCancel (State);
    return UMI_FTS_INVALID;
  }
  Previous = State->Point;
  State->Point.X = (unsigned short)X;
  State->Point.Y = (unsigned short)Y;
  State->Point.Id = (unsigned char)Id;
  State->Point.Active = 1;
  if (!Previous.Active || (Previous.X != State->Point.X) ||
      (Previous.Y != State->Point.Y)) {
    UmiFtsPush (State);
    return UMI_FTS_CHANGED;
  }
  return UMI_FTS_IGNORED;
}

#endif
