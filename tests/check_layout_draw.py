import json
import sys
from pathlib import Path

commands = [json.loads(line) for line in Path(sys.argv[1]).read_text().splitlines()]
texts = [c for c in commands if c['kind'] == 1]
assert len(texts) == 2, texts
hint = next(c for c in texts if c['text'].startswith('No version installed'))
assert hint['text'] == 'No version installed\n未安装版本 😀', hint
assert hint['rect'] == [15, 44, 640, 1000], hint
assert hint['clip'] == [15, 44, 655, 1044], hint
assert hint['size'] == 13 and hint['lines'] == 0 and hint['align'] == 1, hint
assert hint['color'] == 0xFFB3B3BD, hint
status = next(c for c in texts if c['text'] == 'Ready')
assert status['rect'] == [35, 1000, 600, 30], status
fills = [c for c in commands if c['kind'] == 0]
assert len(fills) == 1 and fills[0]['rect'] == hint['rect'], fills
print('layout draw: geometry, UTF-8, font, clipping and hidden progress passed')