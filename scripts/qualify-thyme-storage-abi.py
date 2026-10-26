"""Combine a compiled static ABI reference with checked positive/negative runtime probes."""
import hashlib
import argparse
import json
from pathlib import Path
import struct
import sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--workspace', required=True, type=Path)
p.add_argument('--probes', required=True, type=Path)
p.add_argument('--unixnu', required=True, type=Path)
a = p.parse_args()
ROOT = a.workspace.resolve()
PROBES = a.probes.resolve()
sys.path.insert(0, str(a.unixnu.resolve() / 'kits/NativePrelink'))
import native_prelink as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def runtime(variant, original, model):
    stem = PROBES / ('timed-' + variant)
    report = load(stem.with_suffix('.abi.json'))
    sealed_path = stem.with_suffix('.macho')
    sealed = bytearray(sealed_path.read_bytes())
    crypto = load(stem.with_suffix('.macho.crypto-hmac.json'))
    assert crypto['kernel_sha256'] == sha(sealed_path)
    assert report['source_sha256'] == hashlib.sha256(original).hexdigest()
    assert report['static_audit_sha256'] == sha(ROOT / 'ci-40259996726/static-audit.json')
    site, cave = int(report['site'], 16), int(report['cave'], 16)
    at = lambda va, size: np.backed_offset(model['segments'], va, size, len(original))
    so, co = at(site, 4), at(cave, report['code_bytes'])
    expected_branch = 0x14000000 | (((cave - site) // 4) & 0x3ffffff)
    assert struct.unpack_from('<I', sealed, so)[0] == expected_branch
    code = bytes(sealed[co:co + report['code_bytes']])
    assert hashlib.sha256(code).hexdigest() == report['code_sha256']
    ho = int(crypto['hmac_file_offset'], 16)
    assert sealed[ho:ho + 32].hex() == crypto['hmac_sha256']
    sealed[so:so + 4] = original[so:so + 4]
    sealed[co:co + len(code)] = original[co:co + len(code)]
    sealed[ho:ho + 32] = original[ho:ho + 32]
    assert sealed == original, 'Changes outside the bounded probe and required HMAC'
    manifest = load(Path(str(stem) + '-boot.json'))
    boot = Path(str(stem) + '-boot.img')
    assert sha(boot) == manifest['image_sha256'] and manifest['firmware_shim_fdt_unchanged']
    assert manifest['partition_writes_performed'] is False
    events = load(Path(str(stem) + '-host.json'))
    boots = [e for e in events if e['kind'] == 'temporary_boot']
    returned = [e for e in events if e['kind'] == 'android_return']
    assert len(boots) == len(returned) == 1 and boots[0]['exit_code'] == 0
    assert report['load_storage'] and report['checks_executed']
    return report, code, returned[0]['estimated_linux_start'], manifest['image_sha256']


def main():
    static = load(ROOT / 'ci-40259996726/static-audit.json')
    reference = ROOT / 'candidate-artifact/kernel/kernel.macho'
    prior = load(reference.with_suffix('.json'))
    candidate = ROOT / 'storage-family-candidate.macho'
    original = candidate.read_bytes()
    plan = load(candidate.with_suffix('.json'))
    assert sha(reference) == static['candidate_sha256'] == prior['candidate_sha256']
    assert sha(candidate) == plan['candidate_sha256']
    assert prior['original_sha256'] == plan['original_sha256']
    assert prior['slide'] == plan['slide'] == -0x1000000
    assert prior['original_executable_sha256']['com.apple.iokit.IOStorageFamily'] == plan['original_executable_sha256']['com.apple.iokit.IOStorageFamily']
    assert static['method_slots'] == 190 and static['kernel_bindings'] == 176
    assert static['compiled_public_class_size'] == 0x90 and static['abstract_complete_destructors_trap']
    model = np.macho(original)
    exports = np.exports(original, model)
    names = {name: va for va, group in exports.items() for name in group}
    vtable = int(static['vtable'], 16)
    vo = np.backed_offset(model['segments'], vtable + 16, 190 * 8, len(original))
    pointers = struct.unpack_from('<190Q', original, vo)
    methods = []
    for row, pointer in zip(static['methods'], pointers):
        name = row['reference_name']
        assert pointer == (names[name] if name in names else int(row['candidate_target'], 16)), row
        methods.append({**row, 'candidate_target': hex(pointer), 'kernel_names': exports.get(pointer, [])})
    good, gc, gt, gh = runtime('storage-load-good', original, model)
    bad, bc, bt, bh = runtime('storage-load-negative-short', original, model)
    assert good['negative_class_size'] is False and bad['negative_class_size'] is True
    assert bad['step_seconds'] == 1 and len(gc) == len(bc)
    diffs = [(a, b) for a, b in zip(struct.unpack('<%dI' % (len(gc)//4), gc), struct.unpack('<%dI' % (len(bc)//4), bc)) if a != b]
    assert set(diffs) == {(0x7102417f, 0x7102457f), (76800000, 19200000)}, diffs
    baseline = next(e['estimated_linux_start'] for e in load(PROBES / 'timed-storage-baseline-host.json') if e['kind'] == 'android_return')
    assert abs(gt - baseline) < 1.2, 'Positive runtime check missed the baseline delay'
    assert abs((bt - gt) - 6) < 1.2, 'Deliberate class-size error did not produce its expected distinct delay'
    assert good['bindings']['block_meta'] == static['original_metaclass']
    assert int(good['bindings']['service_meta'], 16) == names['__ZN9IOService10gMetaClassE']
    result = {**static, 'candidate': str(candidate), 'candidate_sha256': plan['candidate_sha256'],
              'source_ioservice_metaclass': good['bindings']['service_meta'], 'methods': methods,
              'live_native_4k': True, 'live_instance_sizes': {'IOBlockStorageDevice': 0x90, 'IOService': 0x88},
              'runtime_evidence_kind': 'checked bsd_init instruction probes with deliberate wrong-size challenge',
              'runtime_evidence': {'baseline_seconds': baseline, 'positive_seconds': gt,
                  'negative_seconds': bt, 'expected_delta_seconds': 6, 'observed_delta_seconds': bt-gt,
                  'positive_boot_sha256': gh, 'negative_boot_sha256': bh,
                  'only_expected_instruction_differences': True, 'bounded_patch_and_hmac_only': True,
                  'all_seven_positive_checks_passed': True, 'normal_kext_load_returned_success': True},
              'hardware_tested': True, 'disk_read_tested': False, 'transport_implemented': False,
              'block_device_published': False, 'root_mounted': False, 'partition_writes_performed': False}
    (ROOT / 'interface-audit.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: compiled static storage ABI plus bounded positive/negative live checks')


if __name__ == '__main__':
    main()
