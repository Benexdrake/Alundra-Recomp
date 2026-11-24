# Disc identity — Alundra Definitive Edition v4 (USA)

Format: **bin/cue, single track, MODE2/2352, NTSC-U (USA)**. Do **not**
convert to ISO: a 2048-byte "cooked" ISO discards the Mode-2 Form-2 XA sectors
that the PlayStation uses for streamed audio and video.

| Field | Value |
| --- | --- |
| Title | Alundra - Definitive Edition v4 (USA), boot file patched |
| Serial | SLUS-00553 (file `SLUS_005.53`) |
| Track | 01, MODE2/2352, data |
| Size (.bin) | 565,580,736 bytes |
| MD5 | `b1a1a01a890cc9f939dd7d14e833bd27` |
| SHA-1 | `e0172f00b07467ff3555fd00e51e593d418820ab` |
| SHA-256 | `34a30c0bb64f2664fcf09b7639ca6c637d855404ccc0870616b7bb954e29b663` |

Hashes computed locally on the development dump (2026-11-24). Your own dump may
differ in layout; the serial is the primary identifier.

Boot EXE: `SLUS_005.53`, entry `0x80036044`. Note that the file on the disc
does not match RAM at runtime; the game loads further code into memory.

Only the North American release (SLUS-00553) is supported. Other regions are untested.

## Where the disc comes from

The supported image is the fan-made *Alundra - Definitive Edition v4 (USA)*
(https://github.com/Pixelplix93/Alundra-Definitive-Edition), an xdelta patch over
*Alundra (USA) (v1.1)*. Its original image (MD5 `5b14a3d8b572cb66d69b7f232099d4ec`)
boots a loader and is **not** accepted. Run

    python tools/make_us_disc.py "Alundra - Definitive Edition v4 (USA).bin" <output dir>

to edit two sectors (SYSTEM.CNF and the `EASY_005.53` directory entry, 54 bytes).
The result has the hashes above. The game then always starts in EASY mode.
