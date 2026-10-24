#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Run the actual RPMh library under host MMIO/time fault injection, no phone."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parent
candidates = [p / rel for p in root.parents for rel in ("Mu_Basecore/MdePkg/Include", "sources/uefi-umi/Mu_Basecore/MdePkg/Include")]
mde = next((p for p in candidates if p.is_dir()), None)
if mde is None:
    raise SystemExit("Initialize the pinned Mu_Basecore submodule before running this test")
with tempfile.TemporaryDirectory(prefix=".rpmh-test-", dir=root) as directory:
    exe = Path(directory) / "test"
    subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fshort-wchar", "-fno-builtin", "-DMDEPKG_NDEBUG",
                    "-fsanitize=address,undefined", "-I", str(mde),
                    "-I", str(mde / "AArch64"), "-I", str(root.parents[1] / "Include"),
                    str(root / "TestUmiRpmh.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True,
                   env={**os.environ, "UBSAN_OPTIONS": "halt_on_error=1",
                        "ASAN_OPTIONS": "abort_on_error=1"})
