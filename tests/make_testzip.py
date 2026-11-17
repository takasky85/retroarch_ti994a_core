#!/usr/bin/env python3
"""Create the deflate-compressed zip used by the inflate tests (needs only the Python stdlib)."""
import sys, zipfile

def gen_a(i):
    return (i * 7 + 3) & 255

seed, rnd = 12345, bytearray()
for _ in range(30000):                       # same LCG as gen_c() in test_main.c
    seed = (seed * 1103515245 + 12345) & 0xFFFFFFFF
    rnd.append((seed >> 16) & 255)

out = sys.argv[1] if len(sys.argv) > 1 else "deflate_test.zip"
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("a.bin", bytes(gen_a(i) for i in range(20000)))
    z.writestr("dir/b.txt", b"The quick brown fox jumps over the lazy dog. " * 40)
    z.writestr("c.bin", bytes(rnd))
    z.writestr("empty.dat", b"")
