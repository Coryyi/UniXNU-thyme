#!/usr/bin/env python3
"""Build a source-only thyme UFS probe on macOS. No firmware, donor or device I/O."""
import argparse
import hashlib
import json
import platform
from pathlib import Path
import shutil
import subprocess
import sys


def git(path, *args):
    return subprocess.check_output(["git", "-C", str(path), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--unixnu", type=Path, required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("-j", "--jobs", type=int, default=6)
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("requires an arm64 macOS build host with the iPhoneOS SDK")
    source = args.unixnu.resolve(strict=True)
    device = Path(__file__).resolve().parents[1]
    commit = git(source, "rev-parse", "HEAD")
    if commit != args.source_commit or len(commit) != 40:
        parser.error("UniXNU HEAD must match the explicit full source commit")
    if git(source, "status", "--porcelain", "--untracked-files=no"):
        parser.error("UniXNU tracked source must be clean")
    patch = source / "patches/xnu/source-corecrypto14-bootstrap.patch"
    if "STARTUP(SYSCTL, STARTUP_RANK_THIRD, source_corecrypto14_bootstrap)" not in patch.read_text():
        parser.error("source must contain the thyme corecrypto startup-order fix")
    workspace = args.workspace.resolve()
    if workspace.exists() or workspace == device or device in workspace.parents:
        parser.error("use a new workspace outside the device repository")
    workspace.mkdir(parents=True)
    (workspace / "devices").mkdir()
    (workspace / "devices/umi").symlink_to(device, target_is_directory=True)
    (workspace / "sources").mkdir()
    (workspace / "sources/UniXNU").symlink_to(source, target_is_directory=True)
    for name in ("tools", "tests"):
        shutil.copytree(device / "bringup" / name, workspace / name)
    record = {
        "profile": "thyme-source-ufs-probe", "unixnu_commit": commit,
        "device_commit": git(device, "rev-parse", "HEAD"),
        "device_dirty": bool(git(device, "status", "--porcelain", "--untracked-files=no")),
        "corecrypto_patch_sha256": hashlib.sha256(patch.read_bytes()).hexdigest(),
        "scope": "source kernel only; no prelink, root mount, firmware or phone boot",
        "legacy_source_lock_applies": False,
        "note": "Separate source-only experiment. Full prelink retains source-lock.json requirements.",
    }
    (workspace / "probe-inputs.json").write_text(json.dumps(record, indent=2) + "\n")
    subprocess.run([sys.executable, str(workspace / "tools/build_umi_xnu.py"),
                    "--defconfig", "thyme_k0_defconfig", "--ufs-screen-report", "--ufs-probe",
                    "--crypto-bootstrap", "source", "-j", str(args.jobs)], check=True)


if __name__ == "__main__":
    main()
