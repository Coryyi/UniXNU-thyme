/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host only: clang -std=c11 -Wall -Wextra -Werror TestFtsEvent.c -o /tmp/umi-fts-test */
#include <assert.h>
#include <stdio.h>
#include "FtsEvent.h"

static void event(unsigned char E[8], unsigned int Kind, unsigned int Id,
                  unsigned int Type, unsigned int X, unsigned int Y)
{
  unsigned int I;
  for (I = 0; I < 8; I++) E[I] = 0;
  E[0] = (unsigned char)Kind;
  E[1] = (unsigned char)((Id << 4) | Type);
  E[2] = (unsigned char)X;
  E[3] = (unsigned char)(((Y & 15) << 4) | (X >> 8));
  E[4] = (unsigned char)(Y >> 4);
}

int main(void)
{
  UMI_FTS_STATE S = {0};
  UMI_FTS_POINT P;
  unsigned char E[8];
  unsigned int I;

  /* A down and up drained in one callback must both survive in order. */
  event(E, 0x13, 2, 1, 1023, 2047);
  assert(UmiFtsParse(&S, E) == UMI_FTS_CHANGED);
  event(E, 0x33, 2, 0, 0, 0);
  assert(UmiFtsParse(&S, E) == UMI_FTS_CHANGED);
  assert(UmiFtsPop(&S, &P) && P.Active && P.X == 1023 && P.Y == 2047);
  assert(UmiFtsPop(&S, &P) && !P.Active && P.X == 1023 && P.Y == 2047);
  assert(!UmiFtsPop(&S, &P));

  /* Motion recovers a missed enter and valid boundary coordinates survive. */
  event(E, 0x23, 1, 2, 1079, 2339);
  assert(UmiFtsParse(&S, E) == UMI_FTS_CHANGED);
  assert(S.Point.X == 1079 && S.Point.Y == 2339);
  event(E, 0x00, 0, 0, 0, 0);
  assert(UmiFtsParse(&S, E) == UMI_FTS_IGNORED && S.Point.Active);

  /* Reset/error clears old samples, never queues a successful tap-up. */
  event(E, 0x03, 0, 0, 0, 0);
  assert(UmiFtsParse(&S, E) == UMI_FTS_READY && !S.Point.Active);
  assert(S.Cancelled && !UmiFtsPop(&S, &P));
  S.Cancelled = 0; /* Driver acknowledges cancellation before scan recovery. */
  event(E, 0x13, 9, 1, 7, 8);
  assert(UmiFtsParse(&S, E) == UMI_FTS_CHANGED);
  event(E, 0xf3, 0, 0, 0, 0);
  assert(UmiFtsParse(&S, E) == UMI_FTS_ERROR && !S.Point.Active);
  assert(S.Point.X == 7 && S.Point.Y == 8 && !S.Count && S.Cancelled);

  /* A second contact cancels the whole gesture; its up cannot confirm it. */
  S = (UMI_FTS_STATE){0};
  event(E, 0x13, 0, 1, 10, 20);
  UmiFtsParse(&S, E);
  event(E, 0x13, 1, 1, 120, 240);
  assert(UmiFtsParse(&S, E) == UMI_FTS_INVALID);
  assert(S.Cancelled && !S.Count && !S.Point.Active);
  event(E, 0x33, 0, 0, 0, 0);
  assert(UmiFtsParse(&S, E) == UMI_FTS_INVALID && !S.Count);

  /* Reject malformed/out-of-range data without generating clicks. */
  S = (UMI_FTS_STATE){0};
  event(E, 0x13, 10, 1, 1, 2);
  assert(UmiFtsParse(&S, E) == UMI_FTS_INVALID && S.Cancelled && !S.Count);
  S = (UMI_FTS_STATE){0};
  event(E, 0xff, 0, 0, 0, 0);
  assert(UmiFtsParse(&S, E) == UMI_FTS_INVALID && S.Cancelled);
  S = (UMI_FTS_STATE){0};
  event(E, 0x13, 0, 1, 1079, 2340);
  assert(UmiFtsParse(&S, E) == UMI_FTS_INVALID && S.Cancelled && !S.Count);
  S = (UMI_FTS_STATE){0};
  event(E, 0x13, 0, 1, 1080, 2339);
  assert(UmiFtsParse(&S, E) == UMI_FTS_INVALID && S.Cancelled && !S.Count);
  S = (UMI_FTS_STATE){0};
  for (I = 3; I <= 5; I++) {
    event(E, 0x13, 0, I, 1, 2);
    assert(UmiFtsParse(&S, E) == UMI_FTS_IGNORED && !S.Point.Active);
  }

  /* Queue overload cancels the entire gesture, including a pending release. */
  S = (UMI_FTS_STATE){0};
  for (I = 0; I < 100; I++) {
    event(E, 0x23, 0, 1, I, I + 100);
    UmiFtsParse(&S, E);
    if (S.Cancelled) break;
  }
  assert(I == 32 && S.Dropped == 1 && S.Cancelled && !S.Count);
  event(E, 0x33, 0, 0, 0, 0);
  UmiFtsParse(&S, E);
  assert(!UmiFtsPop(&S, &P));
  S = (UMI_FTS_STATE){0};
  for (I = 0; I < UMI_FTS_QUEUE_SIZE; I++) {
    event(E, 0x23, 0, 1, I, I + 100);
    UmiFtsParse(&S, E);
  }
  event(E, 0x33, 0, 0, 0, 0);
  UmiFtsParse(&S, E);
  assert(S.Cancelled && !S.Count && !UmiFtsPop(&S, &P));
  puts("FTS event regression: PASS (tap ordering, XY bounds, multi-touch cancellation, reset/error, overflow cancellation)");
  return 0;
}
