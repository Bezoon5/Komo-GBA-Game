import sys
sys.path.insert(0, __import__('os').path.dirname(__file__))
from logo import LOGO, crc16
assert crc16(LOGO) == 0xCF56
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
d[0x04:0x04 + 156] = LOGO
title = b"KOMO" + bytes(8)
d[0xA0:0xAC] = title
d[0xAC:0xB0] = b"BKME"
d[0xB0:0xB2] = b"01"
d[0xB2] = 0x96
d[0xB3] = 0
d[0xB4:0xBC] = bytes(8)
d[0xBC] = 0
chk = 0
for b in d[0xA0:0xBD]:
    chk = (chk + b) & 0xFF
d[0xBD] = (-(chk + 0x19)) & 0xFF
d[0xBE:0xC0] = bytes(2)
while len(d) % 4:
    d.append(0)
open(p, 'wb').write(d)
print("header ok, checksum %02x, size %d KB" % (d[0xBD], len(d) // 1024))
