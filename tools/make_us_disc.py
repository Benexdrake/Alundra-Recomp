#!/usr/bin/env python3
"""Build the disc image this recompilation expects from "Alundra - Definitive
Edition v4 (USA)" (fan patch, https://github.com/Pixelplix93/Alundra-Definitive-Edition).

The Definitive Edition boots a loader (EPIX.EXE) that shows a difficulty menu. The
recompilation needs the game executable itself as the boot file, so two sectors
are edited (54 bytes): the SYSTEM.CNF boot line and the directory entry
EASY_005.53 -> SLUS_005.53. The game always starts in EASY mode. No game data is
included here; you need your own copy of the patched .bin.

Usage: python tools/make_us_disc.py "<Definitive Edition v4 (USA)>.bin" <output dir>
Writes <output dir>/game.bin and <output dir>/Alundra.cue and checks the MD5s.
"""
import hashlib, os, shutil, sys

SRC_SIZE = 565580736
SRC_MD5 = "5b14a3d8b572cb66d69b7f232099d4ec"   # Definitive Edition v4 (USA)
OUT_MD5 = "b1a1a01a890cc9f939dd7d14e833bd27"   # what game.toml / DISC.md expect
PATCHES = [  # (file offset, expected old bytes, new bytes)
    (52127, "45415359", "534c5553"),
    (113977948, "203d206364726f6d3a5c455049582e4558453b310d0a54434220",
                "3d6364726f6d3a5c534c55535f3030352e35333b310d0a544342"),
    (113977975, "20340d0a4556454e54203d2031360d0a535441434b203d20",
                "3420202020200d0a4556454e543d31360d0a535441434b3d"),
]
CUE = 'FILE "game.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n'


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            h.update(block)
    return h.hexdigest()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, out_dir = sys.argv[1], sys.argv[2]
    if os.path.getsize(src) != SRC_SIZE:
        sys.exit("error: %s has the wrong size (expected %d bytes)" % (src, SRC_SIZE))
    if md5(src) != SRC_MD5:
        sys.exit("error: %s is not 'Alundra - Definitive Edition v4 (USA)' (MD5 mismatch)" % src)
    os.makedirs(out_dir, exist_ok=True)
    dst = os.path.join(out_dir, "game.bin")
    shutil.copyfile(src, dst)
    with open(dst, "r+b") as f:
        for offset, old, new in PATCHES:
            f.seek(offset)
            if f.read(len(old) // 2) != bytes.fromhex(old):
                sys.exit("error: unexpected data at offset %d" % offset)
            f.seek(offset)
            f.write(bytes.fromhex(new))
    if md5(dst) != OUT_MD5:
        sys.exit("error: result MD5 mismatch")
    with open(os.path.join(out_dir, "Alundra.cue"), "w", newline="\n") as f:
        f.write(CUE)
    print("OK: wrote %s and Alundra.cue (MD5 %s)" % (dst, OUT_MD5))


if __name__ == "__main__":
    main()
