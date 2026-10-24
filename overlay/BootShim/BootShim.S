/* Derived from uefi-sheng 329b473d BootShim.S; umi-only RAM trace.
 * UMI0: shim reached; UMI1: FD copied. x0/x1 handoff is preserved.
 * Only the reserved console header/data are changed; no pmsg or disk I/O.
 */
_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
_StackBase:
  .quad FD_BASE         /* Text Offset */

_StackSize:
  .quad FD_SIZE + (_Payload - _Head) /* Complete shim and payload */
  .quad 0               /* Flags */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
  /* umi ramoops console header: confirmed in this kernel, ECC disabled. */
  ldr   x12, =0xB0000000
  ldr   w13, =0x43474244
  str   w13, [x12]
  ldr   w13, =0x30494d55
  str   w13, [x12, #12]
  mov   w13, #10
  strb  w13, [x12, #16]
  mov   w13, #5
  str   w13, [x12, #4]
  str   w13, [x12, #8]
  dmb   sy
  dc    cvac, x12
  dsb   sy
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, =FD_BASE

_Entry:
  /* umi ramoops console header: confirmed in this kernel, ECC disabled. */
  ldr   x12, =0xB0000000
  ldr   w13, =0x43474244
  str   w13, [x12]
  ldr   w13, =0x31494d55
  str   w13, [x12, #12]
  mov   w13, #10
  strb  w13, [x12, #16]
  mov   w13, #5
  str   w13, [x12, #4]
  str   w13, [x12, #8]
  dmb   sy
  dc    cvac, x12
  dsb   sy
  /* Publish the copied instructions before executing them. */
  dsb   sy
  mrs   x9, ctr_el0
  ubfx  x9, x9, #16, #4
  mov   x10, #4
  lsl   x9, x10, x9
  sub   x10, x9, #1
  bic   x8, x5, x10
  ldr   x7, =FD_SIZE
  add   x7, x5, x7

_CleanLoop:
  dc    cvac, x8
  add   x8, x8, x9
  cmp   x8, x7
  b.lo  _CleanLoop
  dsb   sy
  ic    iallu
  dsb   sy
  isb
  br    x5

_Dead:
  b     _Dead

.text
.ltorg
.align 4

_Payload:
