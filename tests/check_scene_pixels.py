import struct
import sys
from pathlib import Path

pixels = Path(sys.argv[1]).read_bytes()
assert len(pixels) == 720 * 1280 * 4, len(pixels)
for y in range(1280):
    for x in range(720):
        expected = 0xFF0000FF if 10 <= x < 40 and 20 <= y < 60 else 0xFFFF0000
        actual = struct.unpack_from("<I", pixels, (y * 720 + x) * 4)[0]
        assert actual == expected, (x, y, hex(actual), hex(expected))
print("scene pixels: 921600/921600 matched")
