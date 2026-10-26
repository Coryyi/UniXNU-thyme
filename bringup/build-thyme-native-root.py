#!/usr/bin/env python3
"""Build a thyme native-UFS/APFS candidate from qualified policy inputs; no device I/O."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tomllib


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--workspace', required=True, type=Path)
    p.add_argument('--unixnu', required=True, type=Path)
    p.add_argument('--source-commit', required=True)
    p.add_argument('--inputs', required=True, type=Path)
    p.add_argument('--reference-source', required=True, type=Path)
    p.add_argument('--reference-abi', required=True, type=Path)
    p.add_argument('-j', '--jobs', type=int, default=6)
    a = p.parse_args()
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        p.error('requires arm64 macOS and the iPhoneOS SDK')
    source = a.unixnu.resolve(strict=True)
    device = Path(__file__).resolve().parents[1]
    commit = subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip()
    if commit != a.source_commit or len(commit) != 40:
        p.error('source checkout must match its explicit full commit')
    if subprocess.check_output(['git', '-C', str(source), 'status', '--porcelain', '--untracked-files=no'], text=True).strip():
        p.error('tracked source must be clean')
    workspace = a.workspace.resolve()
    if workspace.exists() or workspace == device or device in workspace.parents:
        p.error('requires a new workspace outside the device repository')
    if sha(a.reference_source) != '8fbd13fa03d05f38009f72d7a464ad9f9fe8eb43d3cd9d1acb16a195ecb19a2c':
        p.error('unexpected ABI reference source kernel')
    workspace.mkdir(parents=True)
    (workspace / 'devices').mkdir()
    (workspace / 'devices/umi').symlink_to(device, target_is_directory=True)
    (workspace / 'sources').mkdir()
    (workspace / 'sources/UniXNU').symlink_to(source, target_is_directory=True)
    for name in ('tools', 'tests'):
        shutil.copytree(device / 'bringup' / name, workspace / name)
    work = workspace / 'out/umi-xnu/thyme'
    (work / 'kernel').mkdir(parents=True)
    (workspace / 'out/umi-xnu/umi').symlink_to('thyme', target_is_directory=True)
    policy = workspace / 'out/umi/root-policy'
    shutil.copytree(a.inputs / 'root-policy', policy)
    media = work / 'userdata'
    media.mkdir()
    shutil.copyfile(a.inputs / 'userdata/userdata-root.json', media / 'userdata-root.json')
    record = json.loads((policy / 'policies.json').read_text())
    if (record['status'] != 'host_verified_owner_integrity_policies' or
            record['root_image_sha256'] != 'fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093'):
        p.error('unexpected root policy provenance')
    for name in ('OS', 'SystemOS'):
        checked = json.loads((policy / name / 'verification.json').read_text())
        if not checked['real_c_signature_and_block_verification'] or sha(policy / name / 'manifest.bin') != checked['manifest_sha256']:
            p.error('root manifest differs from its actual host verification')
    abi = workspace / 'out/umi/storage-abi'
    abi.mkdir(parents=True)
    shutil.copytree(a.reference_abi / 'include', abi / 'include')
    shutil.copyfile(device / 'package/thyme-storage-abi.json', abi / 'interface-audit.json')
    subprocess.run([sys.executable, str(source / 'unixnu'), 'ipsw', 'fetch', 'KernelCache',
                    '-D', str(device), '--out', str(workspace / 'out/umi-xnu')], cwd=source, check=True)
    donor = workspace / 'out/umi-xnu/ipsw/21H461/kernelcache.decompressed'
    if sha(donor) != 'f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7':
        p.error('donor kernel mismatch')
    emit = source / 'kits/NativePrelink/emit_prelink.py'
    def prelink(output, roots, initializers=()):
        command = [sys.executable, str(emit), '--kernel', str(a.reference_source),
                   '--prelinked', str(donor), '--slide=-0x1000000', '--output', str(output)]
        for root in roots:
            command += ['--root', root]
        for initializer in initializers:
            command += ['--early-security-init', initializer]
        subprocess.run(command, check=True)
    reference = work / 'kernel/qemu-storage-apfs.macho'
    prelink(reference, ['com.apple.iokit.IOStorageFamily'])
    if sha(reference) != 'dfd638967a6d2aed6c0bdb5e879e2558d20a31d2180162ac9bfebac418adf2b8':
        p.error('original storage reference differs from the live ABI qualification')
    spec = tomllib.loads((device / 'board.toml').read_text())['kernel']['prelink']
    prelink(work / 'kernel/kernel.macho', spec['roots'], [spec['early_security_init']])
    inputs = {'source_commit': commit,
              'device_commit': subprocess.check_output(['git', '-C', str(device), 'rev-parse', 'HEAD'], text=True).strip(),
              'policy_report_sha256': sha(policy / 'policies.json'),
              'abi_sha256': sha(abi / 'interface-audit.json'),
              'root_image_sha256': record['root_image_sha256'],
              'scope': 'native UFS/APFS compile and prelink; owner-root trust and SystemOS union are not enabled',
              'hardware_tested': False, 'partition_writes_performed': False}
    (workspace / 'native-root-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    subprocess.run([sys.executable, str(workspace / 'tools/build_umi_xnu.py'),
                    '--defconfig', 'thyme_k0_defconfig', '--profile', 'phone',
                    '--crypto-bootstrap', 'prelinked', '--native-ufs', '--pmgr',
                    '--root-samples', str(policy / 'OS/samples.bin'), '-j', str(a.jobs)], check=True)
    subprocess.run([sys.executable, str(workspace / 'tools/build_umi_prelink.py'),
                    '--profile', 'phone', '--storage', '--output', str(work / 'kernel/native-ufs-root.macho')], check=True)


if __name__ == '__main__':
    main()
