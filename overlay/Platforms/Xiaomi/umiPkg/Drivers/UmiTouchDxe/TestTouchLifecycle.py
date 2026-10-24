#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Host fault injection of actual driver cleanup and READY/scan functions.

Only mocks bus/MMIO/time. It does not validate electrical power sequencing.
Run: python3 TestTouchLifecycle.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent
source = (root / "UmiTouch.c").read_text()


def function(name):
    start = re.search(r"STATIC\s+[^{};]*\b" + name + r"\s*\([^{};]*\)\s*\{", source)
    assert start, name
    depth = 1
    pos = start.end()
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start.start():pos]


prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "FtsEvent.h"
#define STATIC static
#define EFIAPI
#define VOID void
#define CONST const
#define TRUE 1
#define FALSE 0
#define BIT7 128U
#define GPIO_OE 512U
#define GPIO_OUT 2U
#define GPIO_CTL(N) (0xf500000U+(N)*4096U)
#define GPIO_IO(N) (GPIO_CTL(N)+4U)
#define MAX_RECORDS 4U
#define DEBUG(X) do {} while (0)
#define EFI_SUCCESS 0
#define EFI_DEVICE_ERROR 7
#define EFI_COMPROMISED_DATA 33
#define EFI_NO_RESPONSE 16
#define EFI_TIMEOUT 18
#define EFI_ERROR(X) ((X) != EFI_SUCCESS)
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef int BOOLEAN;
typedef int EFI_STATUS;
typedef void *EFI_EVENT;
static BOOLEAN mPowerOwned, mOwned, mStopped, mFailed;
static BOOLEAN mVerified, mAwaitReady, mScanPending, mCancelPending;
static UINT32 mPowerCtl[2], mPowerOut[2], mTraceCount;
static UINT64 mReadyStart, now;
static UMI_FTS_STATE mFts;
static UINT8 fifo[2][32];
static BOOLEAN vote_outstanding;
static unsigned withdraw_errors, withdraw_count;
static unsigned scan_count, fifo_index, failures;
static EFI_STATUS bus_status;
static UINT32 MmioRead32(UINT32 a) { (void)a; return 0; }
static VOID MmioWrite32(UINT32 a, UINT32 v) { (void)a; (void)v; }
static BOOLEAN UmiRpmhTouchOutstanding(void) { return vote_outstanding; }
static EFI_STATUS UmiRpmhTouchDisable(void) {
  ++withdraw_count;
  if (withdraw_errors) { --withdraw_errors; return EFI_DEVICE_ERROR; }
  vote_outstanding = FALSE;
  return EFI_SUCCESS;
}
static UINT64 GetPerformanceCounter(VOID) { return now; }
static UINT64 GetTimeInNanoSecond(UINT64 n) { return n; }
static EFI_STATUS AcquirePins(VOID) { assert(0); return EFI_DEVICE_ERROR; }
static VOID Fail(EFI_STATUS status, CONST char *phase) {
  (void)phase; assert(EFI_ERROR(status)); mFailed = TRUE; ++failures;
}
static EFI_STATUS Transfer(CONST UINT8 *w, UINTN wn, UINT8 *r, UINTN rn) {
  if (rn == 0) {
    assert(wn == 3 && w[0] == 0xa0 && w[1] == 0 && w[2] == 1);
    ++scan_count; return EFI_SUCCESS;
  }
  assert(wn == 1 && w[0] == 0x86 && rn <= 24 && fifo_index < 2);
  if (EFI_ERROR(bus_status)) return bus_status;
  memcpy(r, fifo[fifo_index++], rn); return EFI_SUCCESS;
}
'''

tests = r'''
static void reset_test(void) {
  mOwned = TRUE; mStopped = mFailed = mVerified = FALSE;
  mAwaitReady = mScanPending = mCancelPending = FALSE;
  mPowerOwned = vote_outstanding = FALSE;
  mFts = (UMI_FTS_STATE){0};
  memset(fifo, 0, sizeof(fifo));
  now = mReadyStart = 0; scan_count = fifo_index = failures = 0;
  withdraw_errors = withdraw_count = 0;
  bus_status = EFI_SUCCESS;
}
int main(void) {
  reset_test();
  vote_outstanding = TRUE; withdraw_errors = 1;
  RestorePower();
  assert(vote_outstanding && withdraw_count == 1);
  RestorePower();
  assert(!vote_outstanding && withdraw_count == 2);
  RestorePower();
  assert(withdraw_count == 2); /* No duplicate withdrawal after success. */

  reset_test();
  mAwaitReady = TRUE;
  fifo[0][0] = 0x43; fifo[0][7] = 1; /* Earlier non-READY status. */
  fifo[1][0] = 0x03; /* READY in drained record. */
  Poll(NULL, NULL);
  assert(!mAwaitReady && mVerified && mCancelPending && scan_count == 1);
  memset(fifo, 0, sizeof(fifo)); fifo_index = 0;
  Poll(NULL, NULL);
  assert(scan_count == 1 && !failures); /* Never resend initial scan. */

  reset_test();
  mAwaitReady = TRUE;
  Poll(NULL, NULL);
  assert(mAwaitReady && !mVerified && !scan_count && !failures);
  fifo_index = 0; now = 100000000ULL;
  Poll(NULL, NULL);
  assert(mFailed && !mVerified && !scan_count && failures == 1);

  reset_test();
  mAwaitReady = TRUE; bus_status = EFI_NO_RESPONSE;
  now = 20000000ULL;
  Poll(NULL, NULL);
  assert(!mFailed && !mVerified && !scan_count);
  now = 100000000ULL;
  Poll(NULL, NULL);
  assert(mFailed && failures == 1 && !scan_count);

  reset_test(); /* An inherited, already running controller can verify FIFO. */
  Poll(NULL, NULL);
  assert(mVerified && scan_count == 1 && !failures);
  puts("FTS lifecycle: PASS (vote withdrawal retry, READY in drain, timeout, NACK, scan once)");
  return 0;
}
'''

with tempfile.TemporaryDirectory(prefix=".touch-lifecycle-", dir=root) as directory:
    path = Path(directory)
    c = path / "test.c"
    c.write_text(prefix + function("RestorePower") + function("Consume") + function("Poll") + tests)
    exe = path / "test"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(root), str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
