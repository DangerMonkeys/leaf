import re, struct, sys, zlib
from pathlib import Path
p = Path(sys.argv[1])
s = p.read_text()
w, h = map(int, re.findall(r'#define \S+_(?:width|height) (\d+)', s))
data = bytes(int(v, 16) for v in re.findall(r'0x[0-9a-fA-F]{2}', s))
scale = 3
rows = []
for y in range(w):
    row = bytes(0 if data[x*((w+7)//8)+(w-1-y)//8] & (1 << ((w-1-y)%8)) else 255 for x in range(h))
    row = bytes(v for v in row for _ in range(scale))
    rows.extend([b'\0'+row]*scale)
def chunk(kind, data):
    return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data))
p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',h*scale,w*scale,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b''))
