import struct
import sys

BASE = 0x100000000
TEXT_OFF = 0x800
MSG_OFF = 0x900
DATA_VM = BASE + 0x4000
LINK_VM = BASE + 0x8000
LINK_OFF = 0x2000

msg = b"chimera: hello from fixture\n"

code = [
    0xA9BF7BFD,
    0xD2800020,
    0xD2800001,
    0xF2A00001,
    0xF2C00021,
    0xF2800001 | (MSG_OFF << 5),
    0xD2800002 | (len(msg) << 5),
    0xD2800003,
    0xF2A00003,
    0xF2C00023,
    0xF2880003,
    0xF9400063,
    0xD63F0060,
    0xD2800000,
    0xA8C17BFD,
    0xD65F03C0,
]
code_bytes = b"".join(struct.pack("<I", ins) for ins in code)


def section(sect, seg, addr, size, off):
    return struct.pack(
        "<16s16sQQIIIIIIII",
        sect.encode(),
        seg.encode(),
        addr,
        size,
        off,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    )


def segment(name, vmaddr, vmsize, fileoff, filesize, initprot, maxprot, sects):
    body = b"".join(section(s[0], name, s[1], s[2], s[3]) for s in sects)
    cmdsize = 72 + len(body)
    head = struct.pack(
        "<II16sQQQQIIII",
        0x19,
        cmdsize,
        name.encode(),
        vmaddr,
        vmsize,
        fileoff,
        filesize,
        maxprot,
        initprot,
        len(sects),
        0,
    )
    return head + body


pagezero = segment("__PAGEZERO", 0, BASE, 0, 0, 0, 0, [])
text = segment(
    "__TEXT",
    BASE,
    0x1000,
    0,
    0x1000,
    5,
    5,
    [("__text", BASE + TEXT_OFF, len(code_bytes), TEXT_OFF)],
)
data = segment(
    "__DATA",
    DATA_VM,
    0x1000,
    0x1000,
    0x1000,
    3,
    3,
    [("__data", DATA_VM, 8, 0x1000)],
)
link = segment("__LINKEDIT", LINK_VM, 0x1000, LINK_OFF, 0x1000, 1, 1, [])

bind = bytes([0x11, 0x40]) + b"_write\x00" + bytes([0x51, 0x72, 0x00, 0x90, 0x00])
dyld_info = struct.pack("<II", 0x80000022, 48) + struct.pack(
    "<IIIIIIIIII", 0, 0, LINK_OFF, len(bind), 0, 0, 0, 0, 0, 0
)
main_cmd = struct.pack("<IIQQ", 0x80000028, 24, TEXT_OFF, 0)
dylib_name = b"/usr/lib/libSystem.B.dylib\x00"
dylib_size = (24 + len(dylib_name) + 7) & ~7
dylib_cmd = (
    struct.pack("<IIIIII", 0x0C, dylib_size, 24, 0, 0x10000, 0x10000)
    + dylib_name
    + b"\x00" * (dylib_size - 24 - len(dylib_name))
)

cmds = pagezero + text + data + link + dyld_info + main_cmd + dylib_cmd
header = struct.pack(
    "<IIIIIIII",
    0xFEEDFACF,
    0x0100000C,
    0,
    2,
    7,
    len(cmds),
    0x00200085,
    0,
)

img = bytearray(0x3000)
img[0 : len(header)] = header
img[len(header) : len(header) + len(cmds)] = cmds
img[TEXT_OFF : TEXT_OFF + len(code_bytes)] = code_bytes
img[MSG_OFF : MSG_OFF + len(msg)] = msg
img[LINK_OFF : LINK_OFF + len(bind)] = bind

out = sys.argv[1] if len(sys.argv) > 1 else "tests/fixture.ios"
with open(out, "wb") as f:
    f.write(img)
print("wrote", out, len(img), "bytes,", len(cmds), "load command bytes")