# Alundra Recompiled release notes

## v0.1.0 - First preview

First public preview of the German release (SLES-01256) running natively.

- **16:9 widescreen** for the 2D tile renderer, with a soft fade at the map edge, widened clouds and shadow overlay, widened save/load room fog and a stretched ring/light mask.
- **Dark rooms and mines:** the darkness layer is extended to the full width.
- **HUD in the screen corners:** item slots bottom left, hearts top, gilder bottom right; the 10-HP stone is placed correctly.
- **Optional 4:3 side bars** in listed scenes (`pillarbox`, experimental).
- **Fixes:** green blocks in tree tops, a stray gate shadow, tile buffer overflow in 16:9.
- Faster memory-card loading (the BIOS card scan no longer slows loading).
- PAL timing is forced to 60 Hz by default, so the game runs about 20 % faster than on original PAL hardware. `PSX_FORCE_60HZ=0` restores 50 Hz.

Tested: menus, audio and the title-screen movie.

Not yet tested: sprite and object pop-in at the screen edges, dialogs with the new HUD, interiors with a different map width, and the pillarbox room list beyond the listed rooms.

Save states from older builds do not load. Native memory-card saves are unaffected.
You need your own Alundra disc (Germany, SLES-01256); see [DISC.md](DISC.md).
