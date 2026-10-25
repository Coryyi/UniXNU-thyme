#!/usr/bin/env python3
"""Replace the FAT ramdisk in a hash-pinned, previously booted diagnostic image.

No device I/O. Firmware, BootShim, FDT, load addresses and command line remain
byte-identical. Only same-sized ramdisk bytes and Android boot ID may change.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('ramdisk', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--base-sha256', required=True)
    args = parser.parse_args()
    if args.output.exists() or args.output.resolve() in (args.base.resolve(), args.ramdisk.resolve()):
        parser.error('output must be a new path')
    base = args.base.read_bytes()
    ramdisk = args.ramdisk.read_bytes()
    if hashlib.sha256(base).hexdigest() != args.base_sha256:
        parser.error('base boot image hash mismatch')
    if base[:8] != b'ANDROID!':
        parser.error('not an Android boot image')
    kernel_size, _, old_size, _, second, _, _, page, version, _ = struct.unpack_from('<10I', base, 8)
    if (page, version, second) != (4096, 1, 0) or len(ramdisk) != old_size:
        parser.error('requires the retained boot-v1 layout and unchanged ramdisk size')
    if struct.unpack_from('<IQI', base, 1632) != (0, 0, 1648):
        parser.error('unexpected recovery DTBO/header layout')
    start = page * (1 + (kernel_size + page - 1) // page)
    end = start + old_size
    if end != len(base) or ramdisk[510:512] != b'\x55\xaa' or ramdisk[82:90] != b'FAT32   ':
        parser.error('unexpected FAT32 ramdisk or trailing image data')
    kernel = base[page:page + kernel_size]

    def boot_id(payload, recovery_size):
        digest = hashlib.sha1()
        for item in (kernel, payload, b''):
            digest.update(item)
            digest.update(struct.pack('<I', len(item)))
        if recovery_size:
            digest.update(struct.pack('<I', 0))
        return digest.digest() + bytes(12)

    old = base[start:end]
    matches = [variant for variant in (False, True) if boot_id(old, variant) == base[576:608]]
    if len(matches) != 1:
        parser.error('original Android boot ID does not match the verified segment layout')
    result = bytearray(base)
    result[start:end] = ramdisk
    result[576:608] = boot_id(ramdisk, matches[0])
    restored = bytearray(result)
    restored[start:end] = old
    restored[576:608] = base[576:608]
    if restored != base:
        raise ValueError('unintended change outside ramdisk and boot ID')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    record = {
        'base_sha256': args.base_sha256,
        'image_sha256': hashlib.sha256(result).hexdigest(),
        'ramdisk_sha256': hashlib.sha256(ramdisk).hexdigest(),
        'firmware_shim_fdt_kernel_segment_sha256': hashlib.sha256(kernel).hexdigest(),
        'firmware_shim_fdt_unchanged': True, 'ramdisk_bytes': len(ramdisk),
        'hardware_tested': False, 'partition_writes_performed': False,
    }
    args.output.with_suffix('.json').write_text(json.dumps(record, indent=2) + '\n')
    print('Temporary probe image packed; firmware/shim/FDT unchanged; no device I/O')


if __name__ == '__main__':
    main()
