import struct
import sys
from pathlib import Path

pixels = Path(sys.argv[1]).read_bytes()
phase = sys.argv[2]
colors = {"first": 0xFFFF0000, "second": 0xFF0000FF, "push": 0xFF00FF00, "return": 0xFFFF0000}
assert len(pixels) == 720 * 1280 * 4, len(pixels)
for y in range(1280):
    expected = colors[phase] if 44 <= y < 1231 else 0xFF1C1C1F
    row = struct.unpack_from("<720I", pixels, y * 720 * 4)
    assert all(p == expected for p in row), (phase, y, hex(expected), [hex(p) for p in row[:8]])
print(f"container {phase}: 921600/921600 pixels matched")
