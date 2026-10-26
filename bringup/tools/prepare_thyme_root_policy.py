#!/usr/bin/env python3
"""Generate and independently verify owner policies for unchanged thyme OS images."""
import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import platform
import subprocess
import sys
import uuid


class View(ctypes.Structure):
    _fields_ = [('hashes', ctypes.c_void_p), ('image_bytes', ctypes.c_uint64),
                ('block_count', ctypes.c_uint64), ('generation', ctypes.c_uint64),
                ('volume_uuid', ctypes.c_uint8 * 16), ('image_sha256', ctypes.c_uint8 * 32)]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root-report', required=True, type=Path)
    p.add_argument('--raw-dir', required=True, type=Path)
    p.add_argument('--verified-root', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--openssl-prefix', required=True, type=Path)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    kit = a.verified_root.resolve()
    host = out / 'host'
    with (out / 'host-tests.log').open('w') as log:
        subprocess.run([sys.executable, str(kit / 'tests/check.py'), '--output', str(host),
                        '--openssl-prefix', str(a.openssl_prefix)], check=True, stdout=log, stderr=subprocess.STDOUT)
    spec = importlib.util.spec_from_file_location('manifest', kit / 'tools/manifest.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    library = host / ('verified-root.dylib' if platform.system() == 'Darwin' else 'verified-root.so')
    native = ctypes.CDLL(str(library))
    native.vr_host_open.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                                   ctypes.c_uint64, ctypes.POINTER(View)]
    native.vr_host_open.restype = ctypes.c_int
    native.vr_host_block.argtypes = [ctypes.POINTER(View), ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t]
    native.vr_host_block.restype = ctypes.c_int
    media = json.loads(a.root_report.read_text())
    if not media['copied_windows_rehashed'] or media['status'] != 'built_not_flashed':
        raise ValueError('Require the verified, unflashed two-window root image report')
    reports = []
    for window in media['windows']:
        name = window['name']
        if name not in ('OS', 'SystemOS'):
            raise ValueError('Unexpected policy window')
        image = a.raw_dir / (name + '.raw.img')
        if image.stat().st_size != window['size']:
            raise ValueError('Raw input size differs from root image')
        identity = uuid.UUID(window['volume_uuid'])
        destination = out / name
        with image.open('rb', buffering=8 << 20) as stream:
            record = module.make(stream, window['size'], host / 'keys/owner.pem',
                                 host / 'keys/owner-public.pem', identity, 1, destination)
        if record['image_sha256'] != window['sha256']:
            raise ValueError('Raw input differs from the assembled root window')
        envelope = (destination / 'manifest.bin').read_bytes()
        encoded = ctypes.create_string_buffer(envelope)
        key = ctypes.create_string_buffer((destination / 'trusted-key.bin').read_bytes())
        view = View()
        assert native.vr_host_open(encoded, len(envelope), key, 1, ctypes.byref(view)) == 0
        assert bytes(view.volume_uuid) == identity.bytes and view.image_bytes == window['size']
        complete = hashlib.sha256()
        with image.open('rb', buffering=8 << 20) as stream:
            for index in range(view.block_count):
                block = stream.read(4096)
                assert block and native.vr_host_block(ctypes.byref(view), index, block, len(block)) == 0, index
                complete.update(block)
            assert not stream.read(1)
        assert complete.hexdigest() == window['sha256'] and complete.digest() == bytes(view.image_sha256)
        result = {**record, 'name': name, 'blocks_verified': view.block_count,
                  'real_c_signature_and_block_verification': True, 'root_image_offset': window['offset'],
                  'manifest_sha256': sha(destination / 'manifest.bin'), 'root_mounted': False}
        (destination / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
        reports.append(result)
        print(f'{name}: signed envelope and {view.block_count} actual blocks verified by C', flush=True)
    assert len(reports) == 2
    assert (out / 'OS/trusted-key.bin').read_bytes() == (out / 'SystemOS/trusted-key.bin').read_bytes()
    result = {'status': 'host_verified_owner_integrity_policies', 'windows': reports,
              'root_report_sha256': sha(a.root_report), 'root_image_sha256': media['image_sha256'],
              'manifest_bytes': sum((out / w['name'] / 'manifest.bin').stat().st_size for w in reports),
              'source_sha256': {str(path): sha(path) for path in
                  (kit / 'tools/manifest.py', kit / 'src/verified_root.c', kit / 'tests/openssl_provider.c', Path(__file__))},
              'apple_signature_claim': False, 'hardware_tested': False, 'root_mounted': False}
    (out / 'policies.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
