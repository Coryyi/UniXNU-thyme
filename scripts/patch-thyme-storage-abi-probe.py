#!/usr/bin/env python3
"""Read actual storage metaclass metadata at bsd_init and encode checks by reset delay."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--source-sha256', required=True)
    p.add_argument('--static-audit', type=Path)
    p.add_argument('--reference-candidate', type=Path)
    p.add_argument('--negative-class-size', action='store_true')
    p.add_argument('--load-storage', action='store_true', help='request normal synchronous loading of the prelinked storage kext')
    p.add_argument('--step-seconds', type=int, choices=(1,4), default=4)
    a = p.parse_args()
    if a.output.exists() or a.output.resolve() == a.source.resolve():
        p.error('output must be a new diagnostic copy')
    if (a.negative_class_size or a.load_storage) and not a.static_audit:
        p.error('negative challenge requires the static audit')
    if bool(a.static_audit) != bool(a.reference_candidate):
        p.error('static audit and its exact reference candidate must be supplied together')
    data = bytearray(a.source.read_bytes())
    if hashlib.sha256(data).hexdigest() != a.source_sha256:
        p.error('source hash mismatch')
    plan = json.loads(a.source.with_suffix('.json').read_text())
    if (plan['candidate_sha256'] != a.source_sha256 or plan['slide'] != -0x1000000 or
            plan['original_sha256'] != 'f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7'):
        p.error('expected the pinned donor prelink and slide')
    spec = importlib.util.spec_from_file_location('entry_probe', Path(__file__).with_name('patch-xnu-entry-probe.py'))
    probe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probe)
    probe.load_segments(data)
    names = {name: va for va, name in probe.kernel_symbols(a.source).items()}
    site = names['_bsd_init']
    cave = probe.unixthread_pc(data) + 0x78
    offset = probe.file_offset(cave)
    if cave % 4 or any(data[offset:offset + 512]):
        p.error('bounded entry padding is not empty')
    checks = ''
    bindings = {}
    if a.static_audit:
        audit = json.loads(a.static_audit.read_text())
        reference = json.loads(a.reference_candidate.with_suffix('.json').read_text())
        if (hashlib.sha256(a.reference_candidate.read_bytes()).hexdigest() != audit['candidate_sha256'] or
                reference['candidate_sha256'] != audit['candidate_sha256'] or
                reference['original_sha256'] != plan['original_sha256'] or reference['slide'] != plan['slide'] or
                reference['original_executable_sha256']['com.apple.iokit.IOStorageFamily'] !=
                plan['original_executable_sha256']['com.apple.iokit.IOStorageFamily']):
            p.error('static audit must identify the same unchanged donor storage executable and slide')
        if audit['compiled_metaclass_offsets'] != {'superClassLink': 16, 'className': 24, 'classSize': 32}:
            p.error('unexpected compiled metaclass layout')
        meta = int(audit['original_metaclass'], 16)
        extents = [e for e in plan['extents'] if e['module'] == 'com.apple.iokit.IOStorageFamily' and e['segment'] == '__DATA']
        if len(extents) != 1 or not extents[0]['va'] <= meta < extents[0]['va'] + extents[0]['size'] - 36:
            p.error('audited original metaclass is outside the pinned storage data extent')
        bindings = {'block_meta': meta, 'service_meta': names['__ZN9IOService10gMetaClassE']}
        if a.load_storage:
            bindings['load_storage'] = names['_OSKextLoadKextWithIdentifier']
            checks += '''
  adr x0, kext_identifier
  bl load_storage
  mov x10, #8
  cbnz w0, result
'''
        checks += f'''
  mov x10, #1
  mrs x9, tcr_el1
  ubfx x11, x9, #14, #2
  cbnz x11, result
  ubfx x11, x9, #30, #2
  cmp x11, #2
  b.ne result
  mov x10, #2
  mrs x9, sctlr_el1
  tbz x9, #0, result
  mov x10, #3
  mrs x9, ttbr0_el1
  cbz x9, result
  mov x10, #4
  mrs x9, ttbr1_el1
  cbz x9, result
  mov x10, #5
  ldr x9, =block_meta
  ldr x12, =service_meta
  ldr x11, [x9, #16]
  cmp x11, x12
  b.ne result
  mov x10, #6
  ldr w11, [x9, #32]
  cmp w11, #{0x91 if a.negative_class_size else 0x90}
  b.ne result
  mov x10, #7
  ldr w11, [x12, #32]
  cmp w11, #0x88
  b.ne result
'''
    assembly = '.text\n.global _start\n_start:\n' + checks + '''
  mov x10, #0
result:
  ldr x2, =57600000
  ldr x12, =STEP_TICKS
  madd x2, x10, x12, x2
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
    assembly = assembly.replace('STEP_TICKS', str(a.step_seconds * 19200000))
    if a.load_storage:
        assembly += 'kext_identifier:\n  .asciz "com.apple.iokit.IOStorageFamily"\n'
    with tempfile.TemporaryDirectory() as d:
        code = probe.assemble(assembly, cave, bindings, Path(d))
    if len(code) > 512:
        p.error('ABI probe exceeds reserved padding')
    old = struct.unpack_from('<I', data, probe.file_offset(site))[0]
    data[offset:offset + len(code)] = code
    struct.pack_into('<I', data, probe.file_offset(site), probe.branch(site, cave))
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(data)
    a.output.with_suffix('.abi.S').write_text(assembly)
    report = dict(source_sha256=a.source_sha256, unsealed_sha256=hashlib.sha256(data).hexdigest(),
                  site=hex(site), original_word=hex(old), cave=hex(cave), code_bytes=len(code),
                  baseline=a.static_audit is None, negative_class_size=a.negative_class_size,
                  load_storage=a.load_storage,
                  checks_executed=a.static_audit is not None,
                  static_audit_sha256=hashlib.sha256(a.static_audit.read_bytes()).hexdigest() if a.static_audit else None,
                  code_sha256=hashlib.sha256(code).hexdigest(),
                  bindings={k: hex(v) for k, v in bindings.items()},
                  step_seconds=a.step_seconds,
                  delay_seconds=f'3 + first_failed_check*{a.step_seconds}; all checks passed or baseline: 3',
                  checks=['TCR native 4K', 'SCTLR.M', 'TTBR0', 'TTBR1', 'original block parent',
                          'block class size 0x90', 'source service class size 0x88'],
                  partition_writes_performed=False, hardware_tested=False, hmac_reseal_required=True)
    a.output.with_suffix('.abi.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
