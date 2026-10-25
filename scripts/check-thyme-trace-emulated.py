"""Execute the compiled production ARM64 trace writer with only its ring mapped."""
import argparse
from pathlib import Path
import struct
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_LR

def text_section(path):
    data = path.read_bytes()
    magic, _, _, _, count, _, _, _ = struct.unpack_from('<8I', data)
    assert magic == 0xfeedfacf
    pos = 32
    for _ in range(count):
        command, size = struct.unpack_from('<II', data, pos)
        if command == 0x19:
            sections = struct.unpack_from('<I', data, pos + 64)[0]
            for index in range(sections):
                section = pos + 72 + 80 * index
                name, _, _, length, offset = struct.unpack_from('<16s16sQQI', data, section)
                if name.rstrip(b'\0') == b'__text':
                    return data[offset:offset + length]
        pos += size
    raise ValueError('Missing __text')

def run(code, start, size, magic=0x43474244, null=False):
    cpu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    cpu.mem_map(0x100000, 0x10000)
    cpu.mem_write(0x100000, code)
    cpu.mem_map(0x2000000, 0x202000)
    arena = bytearray(b'\xa5' * 0x202000)
    header = 0x2000020
    cap = 0x200000 - 12
    struct.pack_into('<III', arena, 32, magic, start, size)
    cpu.mem_write(0x2000000, bytes(arena))
    cpu.reg_write(UC_ARM64_REG_X0, 0x21)
    cpu.reg_write(UC_ARM64_REG_X1, 0xfedcba9876543210)
    cpu.reg_write(UC_ARM64_REG_X2, 0 if null else header)
    cpu.reg_write(UC_ARM64_REG_LR, 0x10f000)
    cpu.emu_start(0x100000, 0x10f000, count=10000)
    actual = bytes(cpu.mem_read(0x2000000, len(arena)))
    expected = bytearray(arena)
    valid = magic == 0x43474244 and 0 <= start <= size <= cap and not null
    if valid:
        message = b'\nUMI XNU 21 fedcba9876543210\n'
        for i, byte in enumerate(message):
            expected[44 + (start + i) % cap] = byte
        struct.pack_into('<II', expected, 36, (start + len(message)) % cap, min(cap, size + len(message)))
    assert actual == bytes(expected), 'Trace wrote incorrect data or damaged guards'

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('object', type=Path)
    parser.add_argument('--expect-unmapped-write', action='store_true')
    args = parser.parse_args()
    code = text_section(args.object)
    if args.expect_unmapped_write:
        try:
            run(code, 0, 0)
        except UcError as error:
            print(f'CONFIRMED: original writer faults with only console mapped: {error}')
            raise SystemExit(0)
        raise SystemExit('Expected original writer to fault')
    cap = 0x200000 - 12
    cases = [(0,0), (123,456), (cap-5,cap), (cap,cap), (0,0,0), (1,0), (0,cap+1), (0,0,0x43474244,True)]
    for case in cases:
        run(code, *case)
    print(f'PASS: {len(cases)} ARM64 ring executions; no framebuffer/watchdog mapped; exact output and guard bytes verified')
