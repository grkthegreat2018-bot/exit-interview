"""Generate exit_interview.ico (64x64 PNG-in-ICO) - lit doorway + EXIT bar +
figure silhouette, matching the runtime window icon in main.cpp."""
import zlib, struct

W = H = 64
px = [[(8, 9, 12, 255) for _ in range(W)] for _ in range(H)]

def rect(x0, y0, x1, y1, c):
    for y in range(y0, y1):
        for x in range(x0, x1):
            px[y][x] = c

def circle(cx, cy, r, c):
    for y in range(H):
        for x in range(W):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r:
                px[y][x] = c

rect(20, 8, 44, 14, (215, 245, 220, 255))       # EXIT sign bar
rect(22, 18, 42, 52, (150, 228, 170, 255))      # lit doorway
circle(32, 30, 5, (6, 7, 9, 255))               # silhouette head
rect(26, 34, 38, 52, (6, 7, 9, 255))            # silhouette body
rect(20, 52, 44, 55, (90, 150, 110, 255))       # threshold glow

raw = b"".join(b"\x00" + bytes(v for p in row for v in p) for row in px)

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(raw, 9))
       + chunk(b"IEND", b""))

ico = (struct.pack("<HHH", 0, 1, 1)
       + struct.pack("<BBBBHHII", W % 256, H % 256, 0, 0, 1, 32, len(png), 22)
       + png)

open("exit_interview.ico", "wb").write(ico)
open("exit_interview.rc", "w").write('1 ICON "exit_interview.ico"\n')
print("ico bytes:", len(ico))
