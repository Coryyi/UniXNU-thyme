#!/usr/bin/env python3
"""Adapt the official complete read-only UFS block provider, preserving its ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
TREE=ROOT/"out/umi-xnu/umi/tree"
UP=ROOT/"sources/UniXNU"
OUT=TREE/"build/native-ufs"

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def compile_from(record,source,obj,extra):
    command=[]; arguments=record["arguments"]; i=0
    while i<len(arguments):
        v=arguments[i]
        if v==record["file"]: command.append(str(source))
        elif v=="-o": command+=["-o",str(obj)]; i+=1
        elif v in ("-MF","-MJ"): i+=1
        elif v=="-Xclang" and arguments[i+1].startswith("-W"): i+=1
        elif not v.startswith("-W") and v not in ("-MD","-MP"): command.append(v)
        i+=1
    command+=["-Wall","-Wextra","-Werror","-Wno-unused-parameter",
        "-Wno-c99-designator","-Wno-reorder-init-list","-Wno-deprecated-enum-enum-conversion",
        "-mgeneral-regs-only",*extra]
    obj.with_suffix(".command.json").write_text(json.dumps(command,indent=2)+"\n")
    result=subprocess.run(command,cwd=record["directory"],text=True,capture_output=True)
    obj.with_suffix(".log").write_text(result.stdout+result.stderr)
    if result.returncode: raise SystemExit(result.stdout+result.stderr)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile",choices=("qemu","phone"),required=True)
    args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    source=(UP/"drivers/NativeUfs/NativeUfs.cpp").read_text()
    # Keep the qualified root path and add the separately pinned Data store.
    header=source[:source.index("static uint32_t read32")]
    for line in ("  void inspect (void);\n","  void adoptAndRead (unsigned level);\n",
                 "  void dumpFirmwareList (void);\n","  void inspectSmmu (void);\n",
                 "  void inspectContextBanks (void);\n",
                 "  static bool waitSet (uintptr_t base, unsigned offset, uint32_t mask, unsigned loops);\n"):
        assert line in header; header=header.replace(line,"")
    header=header.replace('#include "GccUfs.h"','#include <IOKit/IODeviceTreeSupport.h>')
    header=header.replace('#include "SmmuTakeover.h"','#include "UmiSmmuTakeover.h"')
    header+='extern "C" void umi_native_ufs_checkpoint(void);\n'
    header+='extern "C" unsigned long gPhysBase, gPhysSize;\n'
    probe=(ROOT/"devices/umi/package/UmiUfsProbe.cpp").read_text()
    helpers=probe[probe.index("static uint32_t umi_mmio_upper_seen;"):probe.index('#include "UmiUfsRead.inc"')]
    helpers=helpers.replace("static uintptr_t map(","static uintptr_t mapPhys(")
    helpers+="""
static void write32(uintptr_t base,unsigned offset,uint32_t value)
{
  __asm__ volatile("dsb sy\\n\\tstr %w0,[%1]\\n\\tdsb sy" ::
    "r"(value),"r"(base+offset) : "memory");
}
"""
    match=source[source.index("static bool contains"):source.index("bool NativeUfs::waitSet")]
    match=match.replace("qcom,sm8550-ufshc","qcom,sm8250-ufshc")
    transport=source[source.index("namespace {\n#include \"RootTransportLayout.h\""):
                     source.index("// Bring the live controller up for reading:")]
    transport+='#include "UmiNativeUfsHost.inc"\n}\n'
    layout=source[source.index("namespace {\nstatic uint32_t be32w"):source.index("// ---- Persistent Data store window")]
    # be32w is only used by the excluded legacy panic probe.
    layout=layout[layout.index('#include "RootLayout.h"'):]
    layout="namespace {\n"+layout
    block=source[source.index("// One read-only window"):source.index("// Memory snapshot (native-ufs-probe=13)")]
    block=block.replace("  native_ufs_root_failure = reason;",
                        "  native_ufs_root_failure = reason;\n  umi_native_ufs_checkpoint();")
    block=block.replace("  gPendingProvider->release ();",
                        "  umi_native_ufs_checkpoint();\n  gPendingProvider->release ();")
    block+='#include "UmiNativeUfsTrace.inc"\n'
    start=r'''
bool NativeUfs::start(IOService *provider)
{
  unsigned rootMode=0, diagnostic=0;
  PE_parse_boot_argn("native-ufs-root",&rootMode,sizeof(rootMode));
  PE_parse_boot_argn("native-ufs-diagnostics",&diagnostic,sizeof(diagnostic));
  native_ufs_probe_requested=rootMode;
  if (rootMode!=1 || !OSDynamicCast(IOPlatformExpertDevice,provider)) return false;
  if (!UMI_UFS_PHONE) {
    IOLog("NativeUfs: QEMU hardware access skipped\n");
    umi_native_ufs_checkpoint();
    return false;
  }
  gRootDiagnosticEnabled=diagnostic==1;
  return startRoot(provider);
}
'''
    root=source[source.index("// Boot the root image's windows as block devices."):]
    first=root.index("  gStoreGeometry ="); last=root.index("  const char *names",first)
    # Use the GPT/root geometry already validated by findRootLayout.
    for step in (1,2,3):
        needle=f"  native_ufs_root_step = {step};\n"
        root=root.replace(needle,needle+"  umi_native_ufs_checkpoint();\n")
    needle="  native_ufs_root_step = 3;\n"
    root=root.replace(needle,needle+'''  if (layout->userdataFirst!=0x2a8000 || layout->userdataLast!=0x3b037fa) {
    rootDiagnosticFailure("userdata differs from measured thyme GPT");
    IOFree(layout,sizeof(RootLayout));
    return false;
  }
''')
    generated=OUT/"NativeUfs.cpp"
    store_source=ROOT/"devices/umi/package/data-store/UfsStore.inc"
    store=store_source.read_text()
    for declaration in ("constexpr uint64_t kLogReserveBlocks = 8192;\n", "constexpr uint64_t kExportBlocks = 65536;\n", "uint64_t gLogRegionFirst, gLogRegionBlocks;\n"):
        assert store.count(declaration)==1
        store=store.replace(declaration,"")
    (OUT/"UmiUfsStore.inc").write_text(store)
    generated.write_text(header+helpers+match+transport+layout+'#include "UmiUfsStore.inc"\n'+block+start+root)
    for p in (UP/"drivers/NativeUfs").glob("*.h"): shutil.copyfile(p,OUT/p.name)
    commands=(OUT/"RootCommands.h").read_text()
    old_signature="uint32_t blocks, const void *src)\n"
    assert commands.count(old_signature)==1
    commands=commands.replace(old_signature,"uint32_t blocks, const void *src, bool fua = false)\n")
    assert commands.count("uint8_t cdb[10] = { 0x2a, 0,")==1
    commands=commands.replace("uint8_t cdb[10] = { 0x2a, 0,", "uint8_t cdb[10] = { 0x2a, uint8_t (fua ? 8 : 0),")
    # Phone K1 may read GPT/root windows, but must never issue SCSI writes.
    # Keep QEMU's separate persistence qualification profile unchanged.
    submit_guard="  *ocs = 15;\n  if (!timeoutUs || dataBytes > kDataMax || (dataBytes & 3) ||\n"
    if args.profile == "phone":
        assert commands.count(submit_guard)==1
        commands=commands.replace(submit_guard,
            "  *ocs = 15;\n  if (direction == 1) return kIOReturnNotPrivileged;\n"
            "  if (!timeoutUs || dataBytes > kDataMax || (dataBytes & 3) ||\n")
    (OUT/"RootCommands.h").write_text(commands)
    for name in ("UmiNativeUfsHost.inc","UmiNativeUfsTrace.inc","UmiSmmuTakeover.h"):
        shutil.copyfile(ROOT/"devices/umi/package"/name,OUT/name)
    shutil.copyfile(UP/"drivers/NativeVirtioBlock/VerifiedBlockPolicy.h",OUT/"VerifiedBlockPolicy.h")
    shutil.copyfile(UP/"drivers/NativeUfs/LICENSE",OUT/"LICENSE")
    rec=TREE/"build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json"
    record=json.loads(rec.read_text().strip().rstrip(","))
    compile_from(record,generated,OUT/"NativeUfs.o",[
        "-I",str(ROOT/"out/umi/storage-abi/include"),
        "-I",str(TREE/".cache/xnu-work/osfmk"),
        "-I",str(UP/"kits/VerifiedRoot/include"),
        "-DNATIVE_VERIFIED_BLOCK=1","-DNATIVE_VERIFIED_BLOCK_MULTI_POLICY=1",
        "-DUMI_UFS_PHONE="+str(int(args.profile=="phone"))])
    # The upstream relocation audit checks every slot of the compiled subclass.
    binding=(UP/"scripts/build-native-virtio-bindings.py").read_text()
    binding=binding.replace("ROOT = Path(__file__).resolve().parents[1]",f"ROOT = Path({str(UP)!r})")
    binding=binding.replace("['NativeVirtioBlock', 'NativeRamBlock']","['NativeUfsBlock']")
    binding=binding.replace("default='NativeVirtioBlock'","default='NativeUfsBlock'")
    binding=binding.replace("ROOT / 'build/storage-abi/interface-audit.json'",repr(str(ROOT/"out/umi/storage-abi/interface-audit.json")))
    binding=binding.replace("("+repr(str(ROOT/"out/umi/storage-abi/interface-audit.json"))+").read_text()",
                            "Path("+repr(str(ROOT/"out/umi/storage-abi/interface-audit.json"))+").read_text()")
    binding=binding.replace("out / (component + '.o')","out / 'NativeUfs.o'")
    # Apple's c++filt otherwise strips the Mach-O underscore a second time.
    binding=binding.replace("['xcrun', 'c++filt']","['xcrun', 'c++filt', '-n']")
    binding=binding.replace("ROOT / 'build/native-prelink/native-apfs-load-positive.macho'",
                            f"Path({str(ROOT/'out/umi-xnu/umi/kernel/qemu-storage-apfs.macho')!r})")
    # Require the referenced qualification artifact, unlike the upstream's optional cache.
    binding=binding.replace("if kernel_path.exists() and","if not kernel_path.exists() or")
    binding=binding.replace("out / ('lib' + component + '.a')","out / 'libNativeUfs.a'")
    script=OUT/"bind-storage.py"; script.write_text(binding)
    subprocess.run([sys.executable,str(script),"--directory",str(OUT)],check=True)
    report={"profile":args.profile,"disk_write_path":True,"verified_block_policies":True,
        "data_store_source_sha256":sha(store_source),"data_store_generated_sha256":sha(OUT/"UmiUfsStore.inc"),
        "commands_sha256":sha(OUT/"RootCommands.h"),
        "generated_source_sha256":sha(generated),"object_sha256":sha(OUT/"NativeUfs.o"),
        "archive_sha256":sha(OUT/"libNativeUfs.a"),
        "upstream_sources":{str(p.relative_to(UP)):sha(p) for p in
            [UP/"drivers/NativeUfs/NativeUfs.cpp",UP/"scripts/build-native-virtio-bindings.py",
             *sorted((UP/"drivers/NativeUfs").glob("*.h"))]},
        "device_sources":{str(p.relative_to(ROOT)):sha(p) for p in
            [Path(__file__),store_source,*(ROOT/"devices/umi/package"/n for n in
             ("UmiNativeUfsHost.inc","UmiNativeUfsTrace.inc","UmiSmmuTakeover.h","UmiUfsProbe.cpp"))]},
        "bindings":json.loads((OUT/"bindings.json").read_text()),
        "hardware_tested":False,"root_mounted":False,
        "userdata_geometry":{"first_lba":"0x2a8000","last_lba":"0x3b037fa","lba_bytes":4096},
        "persistent_store_api_compiled":True,
        "phone_scsi_write_guard":{"enabled":args.profile=="phone","blocked_direction":1}}
    (OUT/"build-report.json").write_text(json.dumps(report,indent=2)+"\n")
    print("Built official NativeUfsBlock with umi host adapter and qualified original storage bindings")

if __name__=="__main__": main()
