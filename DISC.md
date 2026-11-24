# Disc identity — Alundra (Germany)

Format: **bin/cue, single track, MODE2/2352, PAL (Germany)**. Do **not**
convert to ISO: a 2048-byte "cooked" ISO discards the Mode-2 Form-2 XA sectors
that the PlayStation uses for streamed audio and video.

| Field | Value |
| --- | --- |
| Title | Alundra (Germany) |
| Serial | SLES-01256 (file `SLES_012.56`) |
| Track | 01, MODE2/2352, data |
| Size (.bin) | 606,049,248 bytes |
| MD5 | `2589bcff97211726b85fad51435b56aa` |
| SHA-1 | `8f3fbe279fcd05659455909ddca7c6671b9030f7` |
| SHA-256 | `61394b58947cf4c8f4ba198401399899ffdd9029b867ef7f8a6a1b7315ff6f71` |

Hashes computed locally on the development dump (2026-11-24), which was
converted from a CHD to a single-track `.cue` + `.bin`. Your own dump may
differ in layout; the serial is the primary identifier.

Boot EXE: `SLES_012.56`, entry `0x800360F4`. Note that the file on the disc
does not match RAM at runtime; the game loads further code into memory.

Only the German release (SLES-01256) is supported. Other regions are untested.
