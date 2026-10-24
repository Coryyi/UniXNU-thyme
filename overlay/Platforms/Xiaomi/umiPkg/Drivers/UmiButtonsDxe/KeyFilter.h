// SPDX-License-Identifier: BSD-2-Clause-Patent
// Hardware-independent debounce/repeat state. Types are supplied by UEFI or
// the host harness. Bits 0/1/2 mean volume up/down/power, respectively.
#ifndef UMI_KEY_FILTER_H
#define UMI_KEY_FILTER_H

typedef struct {
  BOOLEAN Raw;
  BOOLEAN Stable;
  BOOLEAN Armed;
  UINT64 ChangedUs;
  UINT64 PressedUs;
  UINT64 RepeatUs;
} UMI_KEY_STATE;

typedef struct {
  UMI_KEY_STATE Key[3];
} UMI_KEY_FILTER;

STATIC VOID
UmiKeyFilterInit (UMI_KEY_FILTER *Filter, UINT8 Mask, UINT64 NowUs)
{
  UINTN I;
  for (I = 0; I < 3; ++I) {
    Filter->Key[I].Raw = (Mask & (1U << I)) != 0;
    Filter->Key[I].Stable = Filter->Key[I].Raw;
    Filter->Key[I].Armed = FALSE;
    Filter->Key[I].ChangedUs = NowUs;
    Filter->Key[I].PressedUs = NowUs;
    Filter->Key[I].RepeatUs = NowUs;
  }
}

// Returns at most one bit. Power confirms on a short release, allowing the
// hardware long-hold recovery gesture without triggering menu activation.
STATIC UINT8
UmiKeyFilterUpdate (UMI_KEY_FILTER *Filter, UINT8 Mask, UINT64 NowUs)
{
  UINTN I;
  UINT8 Events = 0;
  BOOLEAN Chord = (Mask & (Mask - 1U)) != 0;
  UMI_KEY_STATE *Key;

  for (I = 0; I < 3; ++I) {
    Key = &Filter->Key[I];
    if (Key->Raw != ((Mask & (1U << I)) != 0)) {
      Key->Raw = !Key->Raw;
      Key->ChangedUs = NowUs;
    }
    if (Chord) {
      Key->Armed = FALSE;
    }
    if ((Key->Stable != Key->Raw) && (NowUs - Key->ChangedUs >= 30000)) {
      Key->Stable = Key->Raw;
      if (Key->Stable) {
        Key->Armed = !Chord;
        Key->PressedUs = NowUs;
        Key->RepeatUs = NowUs;
        if ((I != 2) && Key->Armed) {
          Events |= (UINT8)(1U << I);
        }
      } else {
        if ((I == 2) && Key->Armed && (NowUs - Key->PressedUs < 1000000)) {
          Events |= (UINT8)(1U << I);
        }
        Key->Armed = FALSE;
      }
    } else if ((I != 2) && Key->Stable && Key->Raw && Key->Armed &&
               (NowUs - Key->PressedUs >= 500000) &&
               (NowUs - Key->RepeatUs >= 150000)) {
      Key->RepeatUs = NowUs;
      Events |= (UINT8)(1U << I);
    }
  }
  return ((Events & (Events - 1U)) == 0) ? Events : 0;
}
#endif
