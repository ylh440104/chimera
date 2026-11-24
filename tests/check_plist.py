import datetime
import pathlib
import plistlib
import sys

root = pathlib.Path(sys.argv[2])
value = {
    'unicode': '中文 😀<&>',
    'signed': -123456789012345,
    'unsigned': 0xfedcba9876543210,
    'real': 1.25,
    'yes': True,
    'no': False,
    'data': bytes([0, 255, 0, 17, 42]),
    'date': datetime.datetime(2024, 1, 1),
    'array': [7, 'nested', False],
}
if sys.argv[1] == 'prepare':
    root.mkdir(parents=True, exist_ok=True)
    for name, format_value in [('xml', plistlib.FMT_XML), ('binary', plistlib.FMT_BINARY)]:
        (root / ('input.' + name + '.plist')).write_bytes(plistlib.dumps(value, fmt=format_value))
elif sys.argv[1] == 'verify':
    for name in ['xml', 'binary']:
        data = (root / ('output.' + name + '.plist')).read_bytes()
        result = plistlib.loads(data)
        assert result == value, (name, result)
        assert type(result['yes']) is bool and type(result['signed']) is int
        assert type(result['real']) is float and type(result['date']) is datetime.datetime
        print(name, 'independent plistlib verification passed', len(data), 'bytes')
else:
    raise ValueError('Expected prepare or verify')