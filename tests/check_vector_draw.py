import json
import sys
from pathlib import Path

commands = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines()]
layers = [c for c in commands if c['kind'] == 3]
assert len(layers) == 2, layers
plus, minus = layers
assert plus['rect'] == [4, 20, 32, 32], plus
assert plus['clip'] == [12, 20, 36, 52], plus
assert minus['rect'] == [28, 52, 32, 16], minus
assert minus['clip'] == [28, 52, 60, 68], minus
assert plus['text'] and minus['text'] and plus['text'] != minus['text'], layers
assert all(c['color'] == 0xff0a84ff for c in layers), layers
print('image renderer: two paths retained in order with the expected scale and clipping')
