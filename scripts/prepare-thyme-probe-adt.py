#!/usr/bin/env python3
"""Correct only physical board identity in a frozen single-CPU diagnostic ADT."""
import argparse
import hashlib
import json
from pathlib import Path

from umi_adt import decode, load_hardware, string, summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-sha256', required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or args.output.exists():
        parser.error('use a new output path; never replace the frozen ADT')
    data = args.source.read_bytes()
    if hashlib.sha256(data).hexdigest() != args.source_sha256:
        parser.error('frozen ADT hash mismatch')
    tree = decode(data)
    facts = summary(tree)
    if facts['cpu_affinities'] != [0] or facts['root_matching_present']:
        parser.error('expected single-CPU K0 ADT without a root match')
    if facts['loader_window_requested'] != '0x10000000':
        parser.error('unexpected diagnostic memory window')
    product = tree.at('/product')
    previous = product.properties.get('model')
    if previous != string('Qualcomm Technologies, Inc. xiaomi umi'):
        parser.error('expected the frozen inherited umi physical model')
    product.properties['model'] = string(load_hardware()['model'])
    result = tree.encode()
    if decode(result).encode() != result:
        raise ValueError('ADT round-trip differs')
    product.properties['model'] = previous
    if tree.encode() != data:
        raise ValueError('unintended ADT property changes')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    args.output.chmod(0o600)
    record = {
        'source_sha256': args.source_sha256,
        'adt_sha256': hashlib.sha256(result).hexdigest(),
        'changed_properties': ['/product/model'],
        'all_other_properties_preserved': True,
        'hardware_tested': False,
        'note': 'Temporary diagnostic ADT; inherited identity and entropy preserved. Not a production boot tree.',
        **facts,
    }
    args.output.with_suffix(args.output.suffix + '.json').write_text(json.dumps(record, indent=2) + '\n')
    print('thyme probe ADT prepared; only /product/model changed; no device I/O')


if __name__ == '__main__':
    main()
