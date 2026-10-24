#!/usr/bin/env python3
"""Qualify the default-off/no-hardware branch; QEMU has no SM8250 UFS."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--run",required=True,type=Path)
p.add_argument("--kernel",required=True,type=Path)
a=p.parse_args()
run,kernel=a.run.resolve(),a.kernel.resolve()
d=json.loads((run/"gdb-stop.json").read_text())
provenance=json.loads((run/"provenance.json").read_text())
status=json.loads(kernel.with_suffix(".source-build.json").read_text())
symbols={}
for line in subprocess.check_output(["xcrun","nm","-n",kernel],text=True).splitlines():
    fields=line.split()
    if len(fields)==3: symbols[fields[2]]=int(fields[0],16)
assert d["kernel_sha256"]==sha(kernel)==provenance["kernel_sha256"]
assert d["qemu_accelerator"]=="tcg" and not provenance["vmm_property_declared"]
assert not d["bounded_timeout_interrupted"]
assert int(d["pc"],16)==symbols["_umi_boot_progress_checkpoint_eight"]
assert status["ufs_probe"] and not status["ufs_probe_build"]["phone"]
raw=bytes.fromhex(d["memory"][f'{symbols["_umi_ufs_observation"]:#x}:100'])
values=list(struct.unpack("<32Q",raw))
assert values[:4]==[1,2,2,0] and values[4:]==[0]*28, "Must skip every hardware read in QEMU"
archive=run.with_name(run.name+"-ufs-sources")
archive.mkdir()
tree=ROOT/"out/umi-xnu/umi/tree"
shutil.copytree(tree/"build/umi-ufs-probe",archive/"build")
for name,path in {
 "UmiUfsProbe.cpp":ROOT/"devices/umi/package/UmiUfsProbe.cpp",
 "build-umi-ufs-probe.py":ROOT/"devices/umi/scripts/build-umi-ufs-probe.py",
 "build_umi_xnu.py":ROOT/"tools/build_umi_xnu.py",
 "check_umi_ufs_probe_qemu.py":Path(__file__),
 "zzzzzz-umi-ufs-probe.patch":ROOT/"devices/umi/patches/umi-xnu/zzzzzz-umi-ufs-probe.patch",
}.items(): shutil.copy2(path,archive/name)
build=json.loads((archive/"build/build-report.json").read_text())
assert build==status["ufs_probe_build"]
for name,expected in build.get("sources",{}).items():
    source=ROOT/"devices/umi/package"/name
    assert sha(source)==expected
    dest=archive/"sources"/name
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,dest)
for name,expected in build.get("upstream_sources",{}).items():
    source=ROOT/"sources/UniXNU/drivers/NativeUfs"/name
    assert sha(source)==expected
    dest=archive/"upstream"/name
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,dest)
assert sha(archive/"UmiUfsProbe.cpp")==build["source_sha256"]
assert sha(archive/"build-umi-ufs-probe.py")==build["script_sha256"]
index={str(f.relative_to(archive)):sha(f) for f in sorted(archive.rglob("*")) if f.is_file()}
(archive/"sha256.json").write_text(json.dumps(index,indent=2)+"\n")
result={"status":"passed_qemu_ufs_hardware_exclusion","kernel":str(kernel),"kernel_sha256":sha(kernel),
 "observation":values,"source_archive":str(archive.relative_to(ROOT)),
 "source_archive_index_sha256":sha(archive/"sha256.json"),
 "evidence_files":{str(f.relative_to(ROOT)):sha(f) for f in sorted(run.iterdir()) if f.is_file()},
 "hardware_tested":False,"disk_read_tested":False,"apfs_tested":False}
(ROOT/"out/umi/ufs-probe-qemu-baseline.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(result,indent=2))
