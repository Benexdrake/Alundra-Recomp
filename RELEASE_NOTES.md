# Alundra Recompiled release notes

## v2026-11-26 - North America (SLUS-00553)

Versions are now named by date. Same target as 0.2.0: **Alundra - Definitive Edition v4 (USA)**, converted with `tools/make_us_disc.py`.

- Fixes and tweaks to the map-edge fade, background colour fill and widescreen hooks.
- Updated cheat module and new application icon.
- Memory-card loading no longer slows the game down (runtime fix).
- Corrected disc information: the supported disc is the fan-patched Definitive Edition v4, not an unmodified retail disc.

## v0.2.0 - North America (SLUS-00553)

This release targets **Alundra - Definitive Edition v4 (USA)** (fan patch over the US v1.1 disc, serial SLUS-00553), converted with `tools/make_us_disc.py`. The German release (SLES-01256) from the 0.1.0 preview is not part of this package.

- **16:9 widescreen** for the 2D tile renderer, with a soft fade at the map edge, widened clouds and shadow overlay, widened save/load room fog and a stretched ring/light mask.
- **Outside-of-map fill:** where the camera shows more than the map, the area is filled in the colour of the map edge (per-room colours where needed).
- **Dark rooms and mines:** the darkness layer is extended to the full width, including the coal mine rooms.
- **HUD in the screen corners:** item slots bottom left, hearts top, gilder bottom right; the 10-HP stone is placed correctly.
- **Optional 4:3 side bars** in listed scenes (`pillarbox`, experimental).
- **Fixes:** green blocks in tree tops, a stray gate shadow, tile buffer overflow in 16:9, flicker at the edge in the skeleton rooms.

Not yet tested: sprite and object pop-in at the screen edges, dialogs with the new HUD, interiors with a different map width, and rooms not listed as checked in the README.

Save states from older builds do not load. Native memory-card saves are unaffected.
You need your own copy of Alundra - Definitive Edition v4 (USA), converted with `tools/make_us_disc.py` (in the source repository); see [DISC.md](DISC.md). The game always starts in EASY mode.
