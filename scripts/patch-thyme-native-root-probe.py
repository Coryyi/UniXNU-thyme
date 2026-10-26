"""Reset only after the genuine NativeUfs checkpoint reports a measured GPT read.
No UFS layout or executable storage code is changed. No root header is fabricated.
"""
import argparse,hashlib,importlib.util,json,struct,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);p.add_argument('--sha256',required=True);p.add_argument('--negative',action='store_true');a=p.parse_args()
assert not a.output.exists();data=bytearray(a.source.read_bytes());assert hashlib.sha256(data).hexdigest()==a.sha256
spec=importlib.util.spec_from_file_location('probe',Path(__file__).with_name('patch-xnu-entry-probe.py'));m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.load_segments(data)
symbols=m.kernel_symbols(a.source);names={name:va for va,name in symbols.items()};lo,hi=m.function_range(symbols,'_umi_native_ufs_checkpoint');sites=[va for va in range(lo,hi,4) if struct.unpack_from('<I',data,m.file_offset(va))[0]==0xd65f03c0];assert len(sites)==1,sites
cave=m.unixthread_pc(data)+0x78;off=m.file_offset(cave);assert not any(data[off:off+512])
assembly='''.text
.global _start
_start:
 ldr x9, =observation
 ldr x10, [x9,#8]
 cmp x10,#3
 b.ne resume
 mov x10,#1
 ldr x11,[x9,#40]
 cmp x11,#1
 b.ne result
 mov x10,#2
 ldr x11,[x9,#48]
 cbnz x11,result
 mov x10,#3
 ldr x11,[x9,#112]
 ldr x12,=0x2a8000
 cmp x11,x12
 b.ne result
 mov x10,#4
 ldr x11,[x9,#120]
 ldr x12,=0x3b037fa
 cmp x11,x12
 b.ne result
 mov x10,#5
 ldr x11,[x9,#128]
 cmp x11,#EXPECTED_MANIFEST
 b.ne result
 mov x10,#6
 ldr x11,[x9,#152]
 cbnz x11,result
 mov x10,#0
result:
 ldr x12,=76800000
 ldr x2,=57600000
 madd x2,x10,x12,x2
 msr daifset,#3
 isb
 mrs x1,cntpct_el0
 add x2,x1,x2
wait_loop:
 isb
 mrs x1,cntpct_el0
 cmp x1,x2
 b.lo wait_loop
 movz w0,#9
 movk w0,#0x8400,lsl #16
 mov x1,xzr
 mov x2,xzr
 mov x3,xzr
 smc #0
park:
 wfe
 b park
resume:
 ret
.ltorg
'''.replace('EXPECTED_MANIFEST','2' if a.negative else '1')
with tempfile.TemporaryDirectory() as d:code=m.assemble(assembly,cave,{'observation':names['_umi_native_ufs_observation']},Path(d))
assert len(code)<=512;data[off:off+len(code)]=code;struct.pack_into('<I',data,m.file_offset(sites[0]),m.branch(sites[0],cave));a.output.write_bytes(data)
a.output.with_suffix('.native-probe.json').write_text(json.dumps({'source_sha256':a.sha256,'negative':a.negative,'site':hex(sites[0]),'cave':hex(cave),'bytes':len(code),'success_delay':3,'failure_delay':'3+check*4','expected_negative_delay':23,'partition_writes_performed':False,'hardware_tested':False},indent=2))
