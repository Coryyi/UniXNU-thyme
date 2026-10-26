#!/usr/bin/env python3
"""Install a bounded reset experiment at the UFS probe call, without entry/MMU edits.

The before variant skips the UFS call. The after variant calls it normally and
waits delay seconds if observation phase=5/status=0, otherwise delay+12 seconds.
Use mode 1 and thyme-ufs-report=0. This emits timing evidence, not a boot success
claim. Always compare independent challenges against the before control.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-sha256', required=True)
    parser.add_argument('--position', choices=('before', 'after'), required=True)
    parser.add_argument('--delay', type=int, choices=(3, 9), required=True)
    parser.add_argument('--force-mode', type=int, choices=(1, 2), default=1,
                        help='diagnostic copy only; bypass missing UEFI boot arg and set the probe local mode')
    parser.add_argument('--mode2-success-phase', type=int, choices=(15, 16), default=15,
                        help='16 also requires the Recovery first-block SHA-256 comparison')
    parser.add_argument('--encode-ready-flags', action='store_true',
                         help='after successful mode 1, encode HCS/HCE/SMMU readiness in 4-second steps')
    args = parser.parse_args()
    if args.encode_ready_flags and (args.position != 'after' or args.force_mode != 1):
        parser.error('--encode-ready-flags requires --position after --force-mode 1')
    if args.output.exists() or args.output.resolve() == args.source.resolve():
        parser.error('output must be a new kernel copy')
    data = bytearray(args.source.read_bytes())
    if hashlib.sha256(data).hexdigest() != args.source_sha256:
        parser.error('source hash mismatch')
    spec = importlib.util.spec_from_file_location('entry_probe', Path(__file__).with_name('patch-xnu-entry-probe.py'))
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    probe.load_segments(data)
    symbols = probe.kernel_symbols(args.source)
    names = {name: address for address, name in symbols.items()}
    site, callee = probe.find_call(data, symbols, '_IOFindBSDRoot', '_umi_ufs_probe', 1)
    # Refuse an ambiguous second call rather than selecting an arbitrary hook.
    low, high = probe.function_range(symbols, '_IOFindBSDRoot')
    calls = 0
    for address in range(low, high, 4):
        word = struct.unpack_from('<I', data, probe.file_offset(address))[0]
        if word >> 26 == 0b100101:
            immediate = word & 0x3ffffff
            if immediate & (1 << 25):
                immediate -= 1 << 26
            calls += address + immediate * 4 == callee
    if calls != 1:
        parser.error('expected exactly one UFS call in IOFindBSDRoot')
    cave = probe.unixthread_pc(data) + 0x78
    offset = probe.file_offset(cave)
    if cave % 4 or any(data[offset:offset + 512]):
        parser.error('the bounded entry padding is not empty')
    forced_mode_sites = []
    if args.position == 'after':
        # The current DarwinBoot build supplies fixed boot args and omits
        # umi-ufs-probe. Force only the mode local in this diagnostic copy:
        # replace its load with mov w8,#1 and neutralize the parser-failure
        # branch. Keep all device identity and MMIO gates in the probe intact.
        probe_va = names['_umi_ufs_probe']
        mode_load = probe_va + 0x68
        parse_guard = probe_va + 0x40
        expected_load = struct.unpack_from('<I', data, probe.file_offset(mode_load))[0]
        expected_guard = struct.unpack_from('<I', data, probe.file_offset(parse_guard))[0]
        if expected_load != 0xB85CC3A8 or expected_guard != 0x540012C0:
            parser.error('UFS boot-argument prologue changed; refusing forced mode patch')
        struct.pack_into('<I', data, probe.file_offset(mode_load), 0x52800008 | (args.force_mode << 5))
        struct.pack_into('<I', data, probe.file_offset(parse_guard), 0xD503201F)  # nop
        forced_mode_sites = [hex(mode_load), hex(parse_guard)]
    body = ''
    if args.position == 'after':
        observation = names['_umi_ufs_read'] if args.force_mode == 2 else names['_umi_ufs_observation']
        phase_success = args.mode2_success_phase if args.force_mode == 2 else 5
        body = '''  bl callee
  dsb sy
  ldr x9, =observation
  ldr x10, [x9, #8]
  ldr x11, [x9, #16]
''' + f'''  cmp x10, #{phase_success}
  b.ne encode_result
  cbnz x11, encode_result
'''
        if args.encode_ready_flags:
            body += f'''  ldr x9, =observation
  ldr x10, [x9, #80]
  and x10, x10, #1
  ldr x11, [x9, #80]
  ubfx x11, x11, #3, #1
  lsl x11, x11, #1
  orr x10, x10, x11
  ldr x11, [x9, #88]
  and x11, x11, #1
  lsl x11, x11, #2
  orr x10, x10, x11
  ldr x11, [x9, #200]
  cmp x11, #0
  cset x11, ne
  lsl x11, x11, #3
  orr x10, x10, x11
  ldr x2, ={args.delay * 19200000}
  ldr x12, =76800000
  madd x2, x10, x12, x2
  b start_wait
'''
    if args.position == 'after' and args.force_mode == 2:
        encode_failure = '''  ldr x2, =BASE_TICKS
  ldr x12, =76800000
  madd x2, x10, x12, x2
  cmp x11, #0
  cset x11, ne
  ldr x12, =19200000
  madd x2, x11, x12, x2
'''.replace('BASE_TICKS', str(args.delay * 19200000))
        result_delay = f'{args.delay} + phase*4 + (status!=0 ? 1 : 0)'
    else:
        encode_failure = f'''  ldr x2, ={args.delay * 19200000}
  ldr x12, =230400000
  madd x2, x10, x12, x2
  ldr x12, =38400000
  madd x2, x11, x12, x2
'''
        result_delay = ('success: base + 4*ready_flags; bit0=HCS.DEVICE_PRESENT, bit1=HCS.UCRDY, bit2=HCE.ENABLE, bit3=SMMU stream count>0' if args.encode_ready_flags else f'{args.delay} + stage*12 + status*2 (timer ticks at 19.2MHz)')
    assembly = '.text\n.global _start\n_start:\n' + body + f'''  ldr x2, ={args.delay * 19200000}
  b start_wait
encode_result:
{encode_failure}start_wait:
  msr daifset, #3
  isb
  mrs x1, cntpct_el0
  add x2, x1, x2
wait_loop:
  isb
  mrs x1, cntpct_el0
  cmp x1, x2
  b.lo wait_loop
  movz w0, #9
  movk w0, #0x8400, lsl #16
  mov x1, xzr
  mov x2, xzr
  mov x3, xzr
  smc #0
park:
  wfe
  b park
.ltorg
'''
    with tempfile.TemporaryDirectory() as directory:
        work = Path(directory)
        code = probe.assemble(assembly, cave, {'callee': callee, 'observation': observation}, work)
        disassembly = subprocess.check_output(['llvm-objdump', '-d', str(work / 'cave.elf')], text=True)
    if len(code) > 512:
        parser.error('reset thunk exceeds reserved padding')
    original_word = struct.unpack_from('<I', data, probe.file_offset(site))[0]
    data[offset:offset + len(code)] = code
    struct.pack_into('<I', data, probe.file_offset(site), probe.branch(site, cave))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    args.output.with_suffix('.timed.S').write_text(assembly)
    args.output.with_suffix('.timed.disassembly.txt').write_text(disassembly)
    report = {
        'source_sha256': args.source_sha256,
        'unsealed_output_sha256': hashlib.sha256(data).hexdigest(),
        'position': args.position, 'delay_seconds': args.delay,
        'result_delay_seconds': result_delay,
        'encode_ready_flags': args.encode_ready_flags,
        'success_predicate': f"{('umi_ufs_read' if args.force_mode == 2 else 'umi_ufs_observation')}[1] == {(15 if args.force_mode == 2 else 5)} && [2] == 0",
        'call_site': hex(site), 'callee': hex(callee), 'original_call_word': hex(original_word),
        'cave': hex(cave), 'code_bytes': len(code),
        'entry_and_mmu_instructions_changed': False,
        'forced_mode_sites': forced_mode_sites,
        'forced_mode': args.force_mode,
        'forced_mode_scope': 'after variant only; parser result ignored and local mode set explicitly; thyme identity check preserved',
        'observation_array': ('umi_ufs_read' if args.force_mode == 2 else 'umi_ufs_observation'),
        'completion_phase': (15 if args.force_mode == 2 else 5),
        'required_args': ['umi-ufs-probe=1', 'thyme-ufs-report=0'],
        'hmac_reseal_required': True, 'hardware_tested': False,
    }
    args.output.with_suffix('.timed.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
