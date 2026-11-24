# Alundra

<!-- retcomm-readme-metrics -->
[![GitHub downloads (all assets, all releases)](https://img.shields.io/github/downloads/Benexdrake/Alundra-Recomp/total)](https://github.com/Benexdrake/Alundra-Recomp/releases)
[![GitHub downloads (latest release)](https://img.shields.io/github/downloads/Benexdrake/Alundra-Recomp/latest/total)](https://github.com/Benexdrake/Alundra-Recomp/releases/latest)
[![GitHub release](https://img.shields.io/github/v/release/Benexdrake/Alundra-Recomp)](https://github.com/Benexdrake/Alundra-Recomp/releases/latest)
<!-- /retcomm-readme-metrics -->

Static recompilation of **Alundra** built on
[psxrecomp](https://github.com/mstan/psxrecomp) and
[recomp-ui](https://github.com/RetroPortingToolKit/recomp-ui).

Action-adventure by Matrix Software (PlayStation, 1997), recompiled to run natively with modern enhancements.

| Title | Alundra |
|---|---|
| Developer | Matrix Software |
| Publisher (NA) | Working Designs |
| Players | 1 |
| Region | North America (English, SLUS-00553), Definitive Edition v4 |
| Year | 1997 |

## Download

Get the latest build from the [Releases page](https://github.com/Benexdrake/Alundra-Recomp/releases/latest):

- **Windows (x64):** `alundra-vX.Y.Z-windows-x64.zip` - unzip, start `Alundra.exe`.
- **Linux (x86_64), experimental:** `alundra-vX.Y.Z-linux-x86_64.AppImage` - untested beyond the launcher.

No game data is included. You need your own copy of *Alundra - Definitive Edition v4 (USA)*, converted with `tools/make_us_disc.py` (see below and [DISC.md](DISC.md)). The German release (SLES-01256) is not supported by this version.

### Required disc: Alundra Definitive Edition v4 (USA)

This version runs on the fan-made [Alundra Definitive Edition](https://github.com/Pixelplix93/Alundra-Definitive-Edition) v4 (USA), which is itself an xdelta patch over *Alundra (USA) (v1.1)*. An unpatched retail US disc and the German disc are **not** accepted by the launcher.

The Definitive Edition boots a loader (`EPIX.EXE`) with a difficulty menu. The recompilation needs the game executable as the boot file, so two sectors (54 bytes) have to be edited first. Use the helper script:

```
python tools/make_us_disc.py "Alundra - Definitive Edition v4 (USA).bin" C:\Discs\AlundraUS
```

It checks the MD5 of the input and output and writes `game.bin` plus `Alundra.cue`. The game then always starts in EASY mode (no difficulty menu). Other Definitive Edition languages and versions are not supported. See [DISC.md](DISC.md).

## What is built in

**Status: actively played.** Every widescreen-related bug found while playing has been fixed.

### 16:9 widescreen (feature `widescreen`)

- **Wide tile renderer:** the game is a pure 2D tile game, so it runs in `full_2d` mode. The main-program tile loop is widened to 21 columns of 24x16 sprites through instruction hooks.
- **Map edge:** maps are 52 columns wide; the column count is clamped at the map edge, so no garbage or streaks appear beyond it.
- **Soft map edge (feature `map_edge_fade`):** a Gouraud subtractive quad (28 px) fades the world smoothly to black at each map side. It is skipped when no bright tiles lie in its zone (small rooms, dark edge tiles).
- **Outside-of-map fill (feature `background_color`):** where the camera shows more than the map, the area is filled in the colour of the edge column (vertical gradient, median-sampled from VRAM). Under the ring/light mask a fixed per-room colour is used; rooms are recognised by a hash of the map rows.
- **Clouds / shadow overlay** widened to the full 16:9 width (28 KB GPU DMA arena).
- **Dark rooms (mine):** the darkness layer only exists for the 4:3 area, so a flat dark (subtractive, ~40 %) quad is added on each side where the layer has full-height edge columns and nothing beyond them. Mine room 3 uses a fixed per-room fill colour.
- **Skeleton rooms:** in two rooms where edge quads/fill flickered or looked like blocks, nothing is drawn at the edge; the background is the game's clear colour.
- **Tile buffer overflow fix:** with two tile layers the 16:9 tile packets overflowed the game's buffer (VRAM noise); both buffers now live in a private arena. Old save states do not load any more.
- **Save/load room fog** covers the whole width without flicker (shifted copies of the 6 fog strips).
- **Ring / light mask** (120 strips) replaced by quads stretched by 4/3.
- **Gate shadow:** the stray black shadow circle under the gate pillar is collapsed.

### Black side bars in 4:3 scenes (feature `pillarbox`, experimental)

For a list of rooms/scenes that are meant to be shown in 4:3 only (no cutscene flag was found), black bars are drawn left and right in the topmost OT slot. Room list: see `mods/alundra_pillarbox.c`.

### Cheat (feature `cheat_infinite_hp`)

Test aid: keeps HP at the maximum. Off by default.

### HUD in the screen corners (feature `hud_corners`)

Item slots bottom left, hearts top left, gilder bottom right. Handles the 10-HP stone (shown above the small HP dots), the start-menu slide (HUD is hidden while it moves) and the different packet layout before the first menu is opened.

### Speed and loading

- **60 fps** (the NTSC-U version already runs at 60 Hz).

### Not yet tested

Sprite/object pop-in at the edges, dialogs with the new HUD, edge quad on outdoor maps with uniform grass. Interior rooms may have a different map width than 52. Not every room has been checked for the outside-of-map colour.

Features: `widescreen`, `hud_corners`, `background_color`, `map_edge_fade`, `pillarbox`, `cheat_infinite_hp`. Each can be switched off in `mods/state.toml` (`[[feature]] id = "hud_corners"`, `enabled = false`).

### Map checklist (outside-of-map colour per room)

Open work: check every Alundra map for the colour shown where the camera sees more than the map (feature `background_color`). Map list from the [VGMaps Alundra atlas](https://www.vgmaps.com/Atlas/PSX/index.htm). Code: `mods/alundra_bgcolor.c` (`alundra_room_color`, switch on the room hash).

Colours (R,G,B), constants in the code: `ALU_BG_GREEN` (0,8,0), `ALU_BG_RED` (8,0,0, default), `ALU_BG_YELLOW` (8,8,0), `ALU_BG_GRAY` (8,8,8), `ALU_BG_BLACK` (0,0,0).

Per room: note the hash (debug build), the measured colour and the status. Status: `[ ]` open, `[x]` checked (colour correct), `[d]` default is fine.

Known assignments from the code (not yet mapped to the maps below):

- GREEN: 0x793D39CF, 0x1B8E3013 (skeleton), 0x489CF39A, 0xC114DD8E, 0x90C7456E (caves), 0x5944BC3C, 0xA5781E10 (crypt), 0x8A1528CF (stairs), 0x32F8209E (stone room with book), edge hash 0x28BF794D (cave)
- BLACK: 0xD78EE04C, 0x56AA69AF (mine)
- YELLOW: 0x877DDF27 (mine), 0xB2956EA2, 0x7C900F86
- GRAY: 0x0C30BD01
- Sky blue (96,120,136): 0x09B8D459 (outdoor room, clouds on the right)
- Village: 0xB440C2D (special case)

| # | Map | Rooms / hashes | Colour | Status |
|---|-----|----------------|--------|--------|
| 1 | World | | | [ ] |
| 2 | World (In-Game Map) | | | [ ] |
| 3 | Inoa Village | 0xB440C2D (Dorf) + Häuser | Schwarz außen / Häuser Rot | [X] |
| 4 | Inside Coal Mine | | s.o. | [X] |
| 5 | Wendell's Dream | | | [X] |
| 6 | Klark Ship | | | [X] |
| 7 | Bonaire's Dream | | | [ ] |
| 8 | Giles's Dream | | | [ ] |
| 9 | Kline's Nightmare | | | [ ] |
| 10 | Ancient Shrine | | | [ ] |
| 11 | Coastal Cave | | | [ ] |
| 12 | Lars' Crypt | 0x5944BC3C, 0xA5781E10, 0x8A1528CF (vermutlich) | GREEN | [ ] |
| 13 | Reptile Lair | | | [ ] |
| 14 | Tarn's Manor | | | [ ] |
| 15 | Megalith Portal Chamber | | | [ ] |
| 16 | Magyscar | | | [ ] |
| 17 | Meia's Dream | | | [ ] |
| 18 | Watermill | | | [ ] |
| 19 | Fairy Pond | | | [ ] |
| 20 | Inspiration Point | | | [ ] |
| 21 | Magyscar Area | | | [ ] |
| 22 | Nirude's Lair | | | [ ] |
| 23 | Sanctuary Area | | | [ ] |
| 24 | Sanctuary | | | [ ] |
| 25 | Shack | | | [ ] |
| 26 | Underground Waterway | | | [ ] |
| 27 | Cliffs Of Madness | | | [ ] |
| 28 | Coal Mine Area | | | [ ] |
| 29 | Dark Swamp | | | [ ] |
| 30 | Despair Desert N | | | [ ] |
| 31 | Despair Desert S | | | [ ] |
| 32 | Fire Manor | | | [ ] |
| 33 | Mt. Torla & Ice Manor | | | [ ] |
| 34 | Outlook Point | | | [ ] |
| 35 | Pub & Pool | | | [ ] |
| 36 | Tarn's Manor Area | | | [ ] |
| 37 | Elene's Dream | | | [ ] |
| 38 | Murgg Woods | | | [ ] |
| 39 | Torla Mountain | | | [ ] |
| 40 | Tower Of The Great Tree | | | [ ] |
| 41 | Great Tree | | | [ ] |
| 42 | Nestus' Dream | | | [ ] |
| 43 | Nava's Keep | | | [ ] |
| 44 | Beach & Nava's Cabin | | | [ ] |
| 45 | Lake | | | [ ] |
| 46 | Lake Shrine | | | [ ] |
| 47 | Lake Shrine Interior | | | [ ] |

## Screenshots

16:9 widescreen with the HUD in the screen corners.

| | |
|---|---|
| ![Screenshot 1](screenshots/1.png) | ![Screenshot 2](screenshots/2.png) |
| ![Screenshot 3](screenshots/3.png) | ![Screenshot 4](screenshots/4.png) |
| ![Screenshot 5](screenshots/5.png) | ![Screenshot 6](screenshots/6.png) |
| ![Screenshot 7](screenshots/7.png) | ![Screenshot 8](screenshots/8.png) |
| ![Screenshot 9](screenshots/9.png) | |

### Common to all recomps in this family

- **60 fps:** PAL titles are forced to NTSC 60 Hz timing (`PSX_FORCE_60HZ=0` restores 50 Hz); the game runs ~20 % faster than on a PAL console.
- **16:9** is provided by a trusted mod plugin (fixed display aspect 16:9) together with widescreen culling hooks; each feature can be switched off in `mods/state.toml`.

Scaffolded with the New Project Layout. See
`psxrecomp/docs/GAME_PROJECT_SETUP.md` for the full flow.

<!-- retcomm-readme-launcher -->
## Retro Launcher

You can run this title **standalone** (download the release zip, point it at
your disc, play), or manage installs, updates, and disc/BIOS wiring with
**[Retro Launcher](https://github.com/RetroPortingToolKit/Retro-Launcher)** —
the Retro Compilation Manager hub for self-compiling recomps.

[Downloads](https://github.com/RetroPortingToolKit/Retro-Launcher/releases) ·
[Full README & features](https://github.com/RetroPortingToolKit/Retro-Launcher#readme)

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/hub-and-game-launcher.png" alt="Retro hub with a background build, next to a title’s recomp-ui launcher" width="720">
</p>

<p align="center">
  <img src="https://raw.githubusercontent.com/RetroPortingToolKit/Retro-Launcher/main/docs/screenshots/queue-and-background-build.png" alt="Background cmake build with titles queued" width="720">
</p>

Retro checks for updates, installs the prebuilt release zips, and automates
BIOS/ROM/save plumbing so you are not stuck repeating each game’s first run by hand.
<!-- /retcomm-readme-launcher -->

## Legal

You must own the original game. Disc images under `disc/` are gitignored and
must never be committed. Retail BIOS dumps are not redistributed and no C
derived from one may be committed; releases run on the bundled MIT OpenBIOS.

`generated/` (the recompiled game C) **is committed**: releases ship the
compiled game, built by CI from that tree. Regenerate and commit it whenever
seeds or the framework pin change.

Default app icon: `assets/psxrecomp.ico` (and `.png` / `.svg`) — Retro-themed controller mark from `psxrecomp/assets/`. Windows builds embed it via `APP_ICON`.

Optional box art under `launcher_assets/img/` may come from
[libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails)
(`Named_Boxarts`); see `BOXART_SOURCE.txt` when present.

## Quick start (dev)

```bash
git submodule update --init --recursive
./psxrecomp/tools/ci/build_emitters.sh
python3 psxrecomp/psxrecomp_cli.py generate \
  --config game.toml --project-root . --disc disc/<your>.cue
git add generated && git commit -m "Regenerate game C"
# Configures, builds, and stages overlay_toolchain/ beside the runtime so
# streamed overlays can be compiled to native shards (a bare cmake build
# skips that stage and leaves every overlay gap interpreted).
python3 psxrecomp/psxrecomp_cli.py rebuild \
  --config game.toml --project-root . --build-dir build-release --no-pgo
```

Official optimized Windows/Linux releases use the maintained shared builder
with the title disc available. Commit the desired `VERSION`, source and
framework pins, then run:

```bash
scripts/package_release.sh --channel public
# Use --channel playtest for a non-public package; --platform selects Windows/Linux.
```

On Windows PowerShell, use
`psxrecomp/tools/package_title_release.ps1 --channel public`. The generated
`packaging/release.toml` and player instructions define the title payload.
The shared builder owns regeneration, qualified build defaults, fresh private
PGO boot training on supported native toolchains, staging and verification.
Boot training is a build step, not gameplay qualification or a performance
claim. See `psxrecomp/docs/RELEASING_A_TITLE.md`.

The separate *Release builds* CI workflow (tag `vX.Y.Z` or dispatch) builds
committed `generated/` C without a disc on Linux, Windows and macOS. It uses
`scripts/package_prebuilt_release.sh` and attaches compiled-game ZIPs. That
explicit prebuilt path does not train selective PGO and is not the official
optimized-release command. The common builder does not add macOS support.

Studio's current Export action still uses the old prebuilt argument contract;
use the shared command above for optimized releases until Studio adopts the
recipe/channel interface. Its packager detection and `packaging/` warning also
need that adoption; the generated recipe is maintained configuration, not a
forked packager.

## Symbols

Progressive map: `symbols.toml` → `python3 tools/sync_symbols.py` →
`psx_symbols.h` (`PSX_FN_*`). See `psxrecomp/docs/SYMBOLS.md`.

## Framework pins

Submodule gitlinks (`psxrecomp`, optional `recomp-ui`, nested `recomp-net`)
are authoritative. `framework_pins.txt` is an optional scaffold snapshot;
release CI logs SHAs with `record_pins.sh` but builds whatever the gitlinks
resolve to. Bump submodules deliberately — do not float on `main`/`master`
in release CI.

<!-- retcomm-readme-raid -->
---

<p align="center">
  <sub><b>R.A.I.D. — Retro AI Development</b> · a Discord for AI-assisted retro reverse-engineering, decomp &amp; recomp</sub>
</p>

<p align="center">
  <a href="https://discord.gg/Ad9BwSzctP"><img src=".github/raid-discord.png" alt="Join the Retro AI Development (R.A.I.D.) Discord" width="200"></a>
</p>
<!-- /retcomm-readme-raid -->
