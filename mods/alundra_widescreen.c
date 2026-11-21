/*
 * Alundra widescreen enhancement plugin.
 *
 * Widescreen is mod-owned on PSX: the runtime clamps game.toml aspect values
 * back to 4:3 unless a trusted plugin selects a display aspect on activation.
 *
 * Alundra is a 2D tile game. Its background tile pass (guest 0x8002CE..0x8002D440)
 * draws 15 columns of 24 px, positioned from a fixed X table in the main EXE
 * (0x800203F0). To fill a 16:9 view we draw more columns (EXTRA_COLS wider) and
 * start up to LEFT_COLS columns left of the camera, using a private X table.
 */
#include "mod_plugins.h"
#include "cpu_state.h"
#include "gpu.h"

#include <stdio.h>

#define ALU_TILE_W     24
#define ALU_LEFT_COLS  3
#define ALU_EXTRA_COLS 6
#define ALU_TABLE_LEN  (15 + ALU_EXTRA_COLS + 2)

#define R_T7 15
#define R_T8 24
#define R_S6 22
#define R_T5 13

static uint32_t g_alundra_xtab;

/* Cloud/shadow overlay (0x8005C294): 16x16 sprite grid, 21 columns x 16 rows
 * per layer, packet buffer stride 5440. Widen to CLOUD_COLS columns starting
 * CLOUD_LEFT_COLS left of the camera, in a private GPU-DMA packet arena. */
#define ALU_CLOUD_LEFT_COLS 4
#define ALU_CLOUD_COLS      28
#define ALU_CLOUD_ROWS      16
#define ALU_CLOUD_LAYERS    4
#define ALU_CLOUD_PKT       16u
#define ALU_CLOUD_LAYER_SZ  (ALU_CLOUD_COLS * ALU_CLOUD_ROWS * ALU_CLOUD_PKT)
#define ALU_CLOUD_MAPW      40
#define R_V0 2
#define R_A3 7
#define R_T0 8
#define R_T1 9
#define R_FP 30
/* Widening the cloud overlay needs a GPU-DMA aperture arena; merely allocating it
 * corrupts game RAM (tile table entries get overwritten -> green blocks), so it is
 * off until the aperture side effect is understood. */
#define ALU_CLOUD_WIDEN 1
static uint32_t g_alundra_cloud;

/* SUBU t6,t5,t8 (right after ADDU t1,zero,zero) at row start: begin left of column 0 and wrap the map column. */
static void alundra_cloud_row_start(struct CPUState* cpu, uint32_t address) {
    (void)address;
    int32_t a3 = (int32_t)cpu->gpr[R_A3] - ALU_CLOUD_LEFT_COLS;
    while (a3 < 0) a3 += ALU_CLOUD_MAPW;
    cpu->gpr[R_A3] = (uint32_t)a3;
    cpu->gpr[R_T1] = (uint32_t)(-(int32_t)ALU_CLOUD_LEFT_COLS * 16);
}

/* BNE v0,zero (loop back): continue until all columns are drawn. */
static void alundra_cloud_loop(struct CPUState* cpu, uint32_t address) {
    (void)address;
    int32_t t1 = (int32_t)cpu->gpr[R_T1];
    cpu->gpr[R_V0] = (t1 < (ALU_CLOUD_COLS - ALU_CLOUD_LEFT_COLS) * 16) ? 1u : 0u;
}

/* ADDU fp,t1,zero right after t0 = packet base: v0 = layer * 5440. */
static void alundra_cloud_base(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alundra_cloud) return;
    uint32_t layer = cpu->gpr[R_V0] / 5440u;
    if (layer >= ALU_CLOUD_LAYERS) layer = 0;
    cpu->gpr[R_T0] = g_alundra_cloud + layer * ALU_CLOUD_LAYER_SZ;
}

/* Map is 52 columns wide (camera X max 927 = 52*24 - 321). Reading past it hits
 * the next row's data and draws garbage, so clamp the column count to the map end. */
#define ALU_MAP_COLS 52
#define R_A0 4

static uint32_t alundra_clamped_cols(struct CPUState* cpu, int32_t base) {
    int32_t cam = (int32_t)cpu->gpr[R_A0];
    int32_t col = cam < 0 ? 0 : cam / ALU_TILE_W;
    int32_t first = col - (col < ALU_LEFT_COLS ? col : ALU_LEFT_COLS);
    int32_t cols = base + ALU_EXTRA_COLS;
    if (first + cols > ALU_MAP_COLS) cols = ALU_MAP_COLS - first;
    return (uint32_t)(cols < 1 ? 1 : cols);
}

/* SW t8,24($sp): column count in t8 (15, or 14 when fine scroll >= 16). a0 = camera X. */
static void alundra_cols_15(struct CPUState* cpu, uint32_t address) {
    (void)address;
    cpu->gpr[R_T8] = alundra_clamped_cols(cpu, 15);
}
static void alundra_cols_14(struct CPUState* cpu, uint32_t address) {
    (void)address;
    cpu->gpr[R_T8] = alundra_clamped_cols(cpu, 14);
}

/* SW s6,16($sp): s6 = first visible map column. Start up to LEFT_COLS earlier
 * (never before column 0) and rebuild the matching X table. */
static void alundra_first_col(struct CPUState* cpu, uint32_t address) {
    (void)address;
    int32_t col = (int32_t)cpu->gpr[R_S6];
    int32_t shift = col < ALU_LEFT_COLS ? (col < 0 ? 0 : col) : ALU_LEFT_COLS;
    if (!g_alundra_xtab) return;
    cpu->gpr[R_S6] = (uint32_t)(col - shift);
    for (int i = 0; i < ALU_TABLE_LEN; i++)
        psx_mod_write_half(g_alundra_xtab + (uint32_t)i * 2u,
                           (uint16_t)(int16_t)((i - shift) * ALU_TILE_W));
}

/* ADDIU t1,s0,13 right after t7 = &xtab[0]: point t7 at the private table. */
static void alundra_use_xtab(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (g_alundra_xtab) cpu->gpr[R_T7] = g_alundra_xtab;
}

/* Soft map edge: darken the outermost tile columns at the left/right map border by
 * modulating the existing tile sprites (cmd 0x65 raw -> 0x64 modulated, colour < 0x80).
 * No extra geometry. Packet layout (base = ptr-13): +4..6 rgb, +7 cmd, +8 x (s16). */
#define ALU_EDGE_TILE_SHADE 0 /* old stepped tile modulation, replaced by the gradient quads */
#define ALU_CAM_X 0x800E4328u
#define R_T1_ 9
#define R_A1 5
static const uint8_t g_alundra_edge_shade[] = { 0x18, 0x30, 0x4C, 0x66, 0x74 };
#define ALU_EDGE_COLS 6

static void alundra_shade_tile(uint32_t ptr) {
    uint32_t base = ptr - 13u;
    int32_t x = (int16_t)psx_mod_read_half(base + 8u);
    int32_t cam = (int32_t)psx_mod_read_word(ALU_CAM_X);
    int32_t col = (x + cam + ALU_TILE_W / 2) / ALU_TILE_W;
    if (x + cam + ALU_TILE_W / 2 < 0) col = -1;
    int32_t e = col < ALU_MAP_COLS - 1 - col ? col : ALU_MAP_COLS - 1 - col;
    uint32_t w;
    if (e >= 0 && e < ALU_EDGE_COLS - 1) {
        uint32_t c = g_alundra_edge_shade[e];
        w = 0x64000000u | (c << 16) | (c << 8) | c;
    } else {
        w = 0x65808080u;
    }
    psx_mod_write_word(base + 4u, w);
}
static void alundra_edge_tile_a(struct CPUState* cpu, uint32_t address) {
    (void)address; alundra_shade_tile(cpu->gpr[R_T1_]);
}
static void alundra_edge_tile_b(struct CPUState* cpu, uint32_t address) {
    (void)address; alundra_shade_tile(cpu->gpr[R_A1]);
}

/* Soft map edge as one gouraud, semi-transparent (subtractive) quad per side, linked into
 * the last (empty) ordering-table slot so it lies over world tiles/sprites but under the
 * cloud overlay and HUD. Hooked right after the tile pass; sp+0x58 = current OT base. */
#define ALU_MAP_PX       (ALU_MAP_COLS * ALU_TILE_W)
#define ALU_EDGE_W       28
#define ALU_EDGE_SLOT    959u
#define ALU_EDGE_WORDS   10u          /* tag + E1 + 4 * (colour, vertex) */
#define ALU_EDGE_DARK    0xFFu         /* subtracted at the map border */
static uint32_t g_alundra_edge;        /* 2 packets */

static void alundra_edge_build(uint32_t pkt, int32_t x_dark, int32_t x_clear) {
    /* vertices: top-left, top-right, bottom-left, bottom-right */
    int32_t xl = x_dark < x_clear ? x_dark : x_clear;
    int32_t xr = x_dark < x_clear ? x_clear : x_dark;
    uint32_t cl = x_dark < x_clear ? ALU_EDGE_DARK : 0u;
    uint32_t cr = x_dark < x_clear ? 0u : ALU_EDGE_DARK;
    cl *= 0x010101u; cr *= 0x010101u;
    uint32_t yt = 0u, yb = 240u;
    psx_mod_write_word(pkt + 4u,  0xE1000040u);               /* semi mode 2: B - F */
    psx_mod_write_word(pkt + 8u,  cl | 0x3A000000u);
    psx_mod_write_word(pkt + 12u, (yt << 16) | ((uint32_t)xl & 0xFFFFu));
    psx_mod_write_word(pkt + 16u, cr);
    psx_mod_write_word(pkt + 20u, (yt << 16) | ((uint32_t)xr & 0xFFFFu));
    psx_mod_write_word(pkt + 24u, cl);
    psx_mod_write_word(pkt + 28u, (yb << 16) | ((uint32_t)xl & 0xFFFFu));
    psx_mod_write_word(pkt + 32u, cr);
    psx_mod_write_word(pkt + 36u, (yb << 16) | ((uint32_t)xr & 0xFFFFu));
}

/* Small rooms: the map data is 52 columns wide but the room only fills a few of them, the rest
 * is empty clear colour. An edge quad there would just paint a black bar over the (0,8,0) clear
 * fill. So a side is only drawn if a world tile sprite (cmd 0x65) actually overlaps its zone.
 * Walk the chain hanging off each OT slot 5..958 (0..4 = HUD) until it reaches an OT cell. */
#define ALU_OT_SLOTS 960u
static int alundra_tiles_overlap(uint32_t ot, int32_t x0, int32_t x1) {
    /* Rooms whose edge zone only holds black (rgb 0) or one single repeated tile (dark filler
     * next to the room wall) get no quad either: it would only add a black block. A zone
     * counts when it has at least two different lit tiles. */
    uint32_t ot_end = ot + ALU_OT_SLOTS * 4u;
    /* Dark filler columns (same clut and u, only v differs) must not count as different tiles,
     * so compare clut|u only and require four distinct ones. */
    uint32_t seen[4]; int nseen = 0;
    for (uint32_t slot = 5; slot < ALU_EDGE_SLOT; slot++) {
        uint32_t n = psx_mod_read_word(ot + slot * 4u) & 0x00FFFFFFu;
        for (int guard = 0; guard < 64 && n != 0x00FFFFFFu; guard++) {
            uint32_t p = 0x80000000u | n;
            if (p >= ot && p < ot_end) break; /* reached an OT cell */
            uint32_t w1 = psx_mod_read_word(p + 4u);
            if ((w1 >> 24) == 0x65u && (w1 & 0x00FFFFFFu) != 0u) {
                int32_t x = (int16_t)(psx_mod_read_word(p + 8u) & 0xFFFFu);
                if (x < x1 && x + ALU_TILE_W > x0) {
                    uint32_t w3 = psx_mod_read_word(p + 12u);
                    uint32_t key = (w3 & 0xFFFF0000u) | (w3 & 0xFFu);
                    int dup = 0;
                    for (int i = 0; i < nseen; i++) if (seen[i] == key) dup = 1;
                    if (!dup) { seen[nseen++] = key; if (nseen >= 4) return 1; }
                }
            }
            n = psx_mod_read_word(p) & 0x00FFFFFFu;
        }
    }
    return 0;
}

/* Outside the map the game shows the clear colour (0,8,0), while the edge tiles inside have a
 * slightly different dark colour (e.g. grey-brown (8,8,0)): visible hard edge. Where no gradient
 * quad is drawn, paint the outside with the colour the border column has on screen. The colour
 * comes from the previous frame in VRAM (mode of a column just inside the edge). */
static int alundra_edge_sample(int32_t sx, uint32_t* rgb24) {
    GpuDrawingEnvironment env;
    gpu_get_drawing_environment(&env);
    int vx0 = (int)env.area.left, vy0 = (int)env.area.top;
    uint16_t v[15]; int n = 0;
    for (int y = 8; y < 232; y += 16) v[n++] = gpu_vram_peek(vx0 + sx, vy0 + y);
    uint16_t best = v[0]; int bc = 0;
    for (int i = 0; i < n; i++) {
        int c = 0;
        for (int j = 0; j < n; j++) if (v[j] == v[i]) c++;
        if (c > bc) { bc = c; best = v[i]; }
    }
    uint32_t r = (best & 31u) << 3, g = ((best >> 5) & 31u) << 3, b = ((best >> 10) & 31u) << 3;
    *rgb24 = (b << 16) | (g << 8) | r;
    return 1;
}

static uint32_t g_alu_rgb_l, g_alu_rgb_r;
static int g_alu_rgb_l_ok, g_alu_rgb_r_ok;

static void alundra_outside_build(uint32_t pkt, int32_t x0, int32_t x1, uint32_t rgb) {
    psx_mod_write_word(pkt + 4u,  rgb | 0x28000000u);
    psx_mod_write_word(pkt + 8u,  (0u << 16) | ((uint32_t)x0 & 0xFFFFu));
    psx_mod_write_word(pkt + 12u, (0u << 16) | ((uint32_t)x1 & 0xFFFFu));
    psx_mod_write_word(pkt + 16u, (240u << 16) | ((uint32_t)x0 & 0xFFFFu));
    psx_mod_write_word(pkt + 20u, (240u << 16) | ((uint32_t)x1 & 0xFFFFu));
}

/* Stale outermost tile column: the game draws 15 columns, the last one lies outside 4:3. In 16:9
 * it shows. Small scenes (game over) leave stale data there: a column of 16 identical tiles that
 * differs from its neighbour. Copy the neighbour column (u/v/clut) into it. Only x >= 320. */
static int32_t alundra_fix_stale_col(uint32_t ot) {
    uint32_t ot_end = ot + ALU_OT_SLOTS * 4u;
    int32_t xmax = -32768;
    /* pass 0: rightmost tile column */
    for (uint32_t slot = 5; slot < ALU_EDGE_SLOT; slot++) {
        uint32_t n = psx_mod_read_word(ot + slot * 4u) & 0x00FFFFFFu;
        for (int guard = 0; guard < 64 && n != 0x00FFFFFFu; guard++) {
            uint32_t p = 0x80000000u | n;
            if (p >= ot && p < ot_end) break;
            uint32_t w1 = psx_mod_read_word(p + 4u);
            if ((w1 >> 24) == 0x65u && (w1 & 0x00FFFFFFu) != 0u) {
                int32_t x = (int16_t)(psx_mod_read_word(p + 8u) & 0xFFFFu);
                if (x > xmax) xmax = x;
            }
            n = psx_mod_read_word(p) & 0x00FFFFFFu;
        }
    }
    if (xmax < 0) return -32768;
    if (xmax < 320 || xmax > 320 + 2 * ALU_TILE_W) return xmax;
    /* pass 1: is the column uniform, collect neighbour column per row */
    uint32_t nbr[16] = {0}, key = 0, nbr_has = 0; int cnt = 0, uniform = 1, same_as_nbr = 1;
    for (uint32_t slot = 0; slot < ALU_EDGE_SLOT; slot++) {
        uint32_t n = psx_mod_read_word(ot + slot * 4u) & 0x00FFFFFFu;
        for (int guard = 0; guard < 64 && n != 0x00FFFFFFu; guard++) {
            uint32_t p = 0x80000000u | n;
            if (p >= ot && p < ot_end) break;
            uint32_t w1 = psx_mod_read_word(p + 4u);
            if ((w1 >> 24) == 0x65u && (w1 & 0x00FFFFFFu) != 0u) {
                uint32_t w2 = psx_mod_read_word(p + 8u);
                int32_t x = (int16_t)(w2 & 0xFFFFu);
                uint32_t row = (w2 >> 16) >> 4;
                uint32_t w3 = psx_mod_read_word(p + 12u);
                if (x == xmax) { if (!cnt) key = w3; else if (w3 != key) uniform = 0; cnt++; }
                else if (x == xmax - ALU_TILE_W && row < 16) { nbr[row] = w3; nbr_has |= 1u << row; }
            }
            n = psx_mod_read_word(p) & 0x00FFFFFFu;
        }
    }
    if (!uniform || cnt < 16 || nbr_has == 0u) return xmax;
    for (int r = 0; r < 16; r++) if ((nbr_has >> r & 1u) && nbr[r] != key) same_as_nbr = 0;
    if (same_as_nbr) return xmax;
    /* pass 2: copy neighbour's texture word into the stale column */
    for (uint32_t slot = 0; slot < ALU_EDGE_SLOT; slot++) {
        uint32_t n = psx_mod_read_word(ot + slot * 4u) & 0x00FFFFFFu;
        for (int guard = 0; guard < 64 && n != 0x00FFFFFFu; guard++) {
            uint32_t p = 0x80000000u | n;
            if (p >= ot && p < ot_end) break;
            uint32_t w1 = psx_mod_read_word(p + 4u);
            if ((w1 >> 24) == 0x65u && (w1 & 0x00FFFFFFu) != 0u) {
                uint32_t w2 = psx_mod_read_word(p + 8u);
                uint32_t row = (w2 >> 16) >> 4;
                if ((int16_t)(w2 & 0xFFFFu) == xmax && row < 16 && (nbr_has >> row & 1u))
                    psx_mod_write_word(p + 12u, nbr[row]);
            }
            n = psx_mod_read_word(p) & 0x00FFFFFFu;
        }
    }
    return xmax;
}

static void alundra_after_tiles(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alundra_edge) return;
    uint32_t ot = psx_mod_read_word(cpu->gpr[29] + 0x58u);
    if ((ot & 0xFF000000u) != 0x80000000u) return;
    int32_t xmax = alundra_fix_stale_col(ot);
    int32_t cam = (int32_t)psx_mod_read_word(ALU_CAM_X);
    int32_t m = psx_mod_widescreen_view_x_margin();
    int32_t xl = -cam, xr = ALU_MAP_PX - cam;
    uint32_t slot = ot + ALU_EDGE_SLOT * 4u;
    uint32_t head = psx_mod_read_word(slot);
    uint32_t next = head & 0x00FFFFFFu;
    uint32_t first = 0;
    /* build right first so that the left ends up first in the chain */
    int32_t xrs = xr, xls = xl;
    int drew_r = 0, drew_l = 0;
    if (xrs - ALU_EDGE_W < 320 + m && xrs > -m && alundra_tiles_overlap(ot, xrs - ALU_EDGE_W, xrs)) {
        drew_r = 1;
        uint32_t pkt = g_alundra_edge + 64u;
        alundra_edge_build(pkt, xrs, xrs - ALU_EDGE_W);
        psx_mod_write_word(pkt, ((ALU_EDGE_WORDS - 1u) << 24) | next);
        next = pkt & 0x00FFFFFFu;
        first = pkt;
    }
    if (xls + ALU_EDGE_W > -m && xls < 320 + m && alundra_tiles_overlap(ot, xls, xls + ALU_EDGE_W)) {
        drew_l = 1;
        uint32_t pkt = g_alundra_edge;
        alundra_edge_build(pkt, xls, xls + ALU_EDGE_W);
        psx_mod_write_word(pkt, ((ALU_EDGE_WORDS - 1u) << 24) | next);
        next = pkt & 0x00FFFFFFu;
        first = pkt;
    }
    uint32_t rgb;
    if (xr <= 316 && xr > 4) { if (alundra_edge_sample(xr - 3, &rgb)) { g_alu_rgb_r = rgb; g_alu_rgb_r_ok = 1; } }
    if (!drew_r && xr > 4 && xr < 320 + m && g_alu_rgb_r_ok) {
        rgb = g_alu_rgb_r;
        uint32_t pkt = g_alundra_edge + 192u;
        alundra_outside_build(pkt, xr, 320 + m, rgb);
        psx_mod_write_word(pkt, (5u << 24) | next);
        next = pkt & 0x00FFFFFFu;
        first = pkt;
    }
    if (xl >= -1 && xl < 316) { if (alundra_edge_sample(xl + 2, &rgb)) { g_alu_rgb_l = rgb; g_alu_rgb_l_ok = 1; } }
    if (!drew_l && xl > -m && xl < 316 && g_alu_rgb_l_ok) {
        rgb = g_alu_rgb_l;
        uint32_t pkt = g_alundra_edge + 128u;
        alundra_outside_build(pkt, -m, xl, rgb);
        psx_mod_write_word(pkt, (5u << 24) | next);
        next = pkt & 0x00FFFFFFu;
        first = pkt;
    }
    /* tiles end inside the 16:9 view (small scenes draw only 15 columns): fill the gap */
    if (xr >= 320 + m && xmax >= 0 && xmax + ALU_TILE_W < 320 + m &&
        alundra_edge_sample(316, &rgb)) {
        uint32_t pkt = g_alundra_edge + 192u;
        alundra_outside_build(pkt, xmax + ALU_TILE_W, 320 + m, rgb);
        psx_mod_write_word(pkt, (5u << 24) | next);
        next = pkt & 0x00FFFFFFu;
        first = pkt;
    }
    if (first)
        psx_mod_write_word(slot, (head & 0xFF000000u) | (first & 0x00FFFFFFu));
}

/* Save/load room cloud layer: six 64x128 semi-trans sprite strips (cmd 0x66, texture period
 * 320 px = 5 strips) drawn through the generic object draw function in the main EXE. The
 * six strips only cover the 4:3 view, so for 16:9 we add shifted +-320 px copies (same u/v
 * and texpage, which repeat with period 320) of the strip that continues the row at each end.
 * Hooked right after the function's last OT link (0x800262EC); fp = object, s0 = sprite
 * packet, s1 = its E1 texpage packet, OT base pointer at 0x8015AE90, slot = fp.s16[4]*4+0x70. */
#define ALU_STRIP_OT_PTR 0x8015AE90u
#define ALU_STRIP_PERIOD 320
static uint32_t g_alundra_strip; /* 2 x 32 B: E1 packet (8 B) + sprite packet (24 B) */

static void alundra_strip_emit(uint32_t slot_addr, uint32_t s0, uint32_t s1,
                               int32_t new_x, uint32_t pkt) {
    uint32_t e1 = pkt, spr = pkt + 8u;
    psx_mod_write_word(e1 + 4u, psx_mod_read_word(s1 + 4u));
    for (uint32_t i = 4u; i < 20u; i += 4u)
        psx_mod_write_word(spr + i, psx_mod_read_word(s0 + i));
    psx_mod_write_half(spr + 8u, (uint16_t)(int16_t)new_x);
    uint32_t head = psx_mod_read_word(slot_addr);
    /* head-insert the sprite, then the E1 packet in front of it (same order as the game) */
    psx_mod_write_word(spr, (psx_mod_read_word(s0) & 0xFF000000u) | (head & 0x00FFFFFFu));
    psx_mod_write_word(slot_addr, (head & 0xFF000000u) | (spr & 0x00FFFFFFu));
    head = psx_mod_read_word(slot_addr);
    psx_mod_write_word(e1, (psx_mod_read_word(s1) & 0xFF000000u) | (head & 0x00FFFFFFu));
    psx_mod_write_word(slot_addr, (head & 0xFF000000u) | (e1 & 0x00FFFFFFu));
}

static void alundra_cloud_strip(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alundra_strip) return;
    uint32_t fp = cpu->gpr[R_FP];
    if (psx_mod_read_word(fp + 8u) != 0xFFFFFFFFu) return;
    uint32_t s0 = cpu->gpr[16], s1 = cpu->gpr[17];
    if ((s0 & 0xFF000000u) != 0x80000000u || (s1 & 0xFF000000u) != 0x80000000u) return;
    if ((psx_mod_read_word(s0 + 4u) >> 24) != 0x66u) return;
    if (psx_mod_read_half(s0 + 0x10u) != 64u || psx_mod_read_half(s0 + 0x12u) != 128u) return;
    int32_t x = (int16_t)psx_mod_read_half(s0 + 8u);
    int32_t m = psx_mod_widescreen_view_x_margin();
    uint32_t ot = psx_mod_read_word(ALU_STRIP_OT_PTR);
    if ((ot & 0xFF000000u) != 0x80000000u) return;
    uint32_t slot = ot + (uint32_t)((int32_t)(int16_t)psx_mod_read_half(fp + 4u) * 4) + 0x70u;
    /* leftmost strip x0 is in [-64, 0) (the game wraps it at -64); strip j=1 (0 <= x < 64)
     * continues past the right end, strip j=4 (192 < x < 256) continues past the left end. */
    if (x >= 0 && x < 64 && x + ALU_STRIP_PERIOD < 320 + m)
        alundra_strip_emit(slot, s0, s1, x + ALU_STRIP_PERIOD, g_alundra_strip + 32u);
    if (x > 192 && x < 256 && x - ALU_STRIP_PERIOD + 64 > -m)
        alundra_strip_emit(slot, s0, s1, x - ALU_STRIP_PERIOD, g_alundra_strip);
}

/* Ring/light mask overlay (e.g. the dark tower rooms): 120 semi-trans variable sprites (cmd 0x67,
 * height 4, width <= 256) cut from one 256x240 4-bit mask texture, built by the generic object
 * function at 0x8005D3xx and linked into OT slot 0. They only span 4:3. Sprites cannot be scaled,
 * so at the function end (JR ra, 0x8005D5E0; a3 = sprite, t6 = OT slot ptr, t3 = layer index)
 * each such sprite is replaced by a textured quad (cmd 0x2F) stretched 4/3 about x = 160, which
 * fills the 16:9 view. Texpage = same table the game uses for its E1 packet (0x8018CF66 + 2*idx). */
#define ALU_MASK_STRETCH_NUM 4
#define ALU_MASK_STRETCH_DEN 3
#define ALU_MASK_QUADS       480
#define ALU_MASK_QUAD_SZ     48u
#define R_T3 11
#define R_T6 14
static uint32_t g_alundra_mask;
static uint32_t g_alundra_mask_idx;

static int32_t alundra_mask_sx(int32_t x) {
    int32_t d = (x - 160) * ALU_MASK_STRETCH_NUM;
    return 160 + (d >= 0 ? (d + 1) / ALU_MASK_STRETCH_DEN : -((-d + 1) / ALU_MASK_STRETCH_DEN));
}

static void alundra_mask_stretch(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alundra_mask) return;
    uint32_t spr = cpu->gpr[R_A3], slot = cpu->gpr[R_T6];
    if ((spr & 0xFF000000u) != 0x80000000u || (slot & 0xFF000000u) != 0x80000000u) return;
    if ((psx_mod_read_word(spr + 4u) >> 24) != 0x67u) return;
    uint32_t wh = psx_mod_read_word(spr + 16u);
    uint32_t w = wh & 0xFFFFu, h = wh >> 16;
    if (h != 4u || w == 0u || w > 256u) return;
    /* only when the game just linked this sprite at the head of its slot */
    if ((psx_mod_read_word(slot) & 0x00FFFFFFu) != (spr & 0x00FFFFFFu)) return;
    uint32_t layer = cpu->gpr[R_T3];
    if (layer > 8u) return;
    uint32_t def = psx_mod_read_word(0x8018CF98u + layer * 4u);
    if ((def & 0xFF000000u) != 0x80000000u) return;
    uint32_t idx = psx_mod_read_byte(def + 2u);
    uint32_t tp = psx_mod_read_half(idx ? 0x8018CF66u + idx * 2u : 0x8018CF68u) & 0x01FFu;

    uint32_t xy = psx_mod_read_word(spr + 8u);
    int32_t x = (int16_t)(xy & 0xFFFFu), y = (int16_t)(xy >> 16);
    uint32_t uvc = psx_mod_read_word(spr + 12u);
    uint32_t u = uvc & 0xFFu, v = (uvc >> 8) & 0xFFu, clut = uvc >> 16;
    uint32_t u1 = u + w; if (u1 > 255u) u1 = 255u;
    uint32_t v1 = v + 4u; if (v1 > 255u) v1 = 255u;
    uint32_t x0 = (uint32_t)alundra_mask_sx(x) & 0xFFFFu;
    uint32_t x1 = (uint32_t)alundra_mask_sx(x + (int32_t)w) & 0xFFFFu;
    uint32_t ya = ((uint32_t)y & 0xFFFFu) << 16, yb = ((uint32_t)(y + 4) & 0xFFFFu) << 16;

    uint32_t q = g_alundra_mask + (g_alundra_mask_idx++ % ALU_MASK_QUADS) * ALU_MASK_QUAD_SZ;
    psx_mod_write_word(q, (9u << 24) | (psx_mod_read_word(spr) & 0x00FFFFFFu));
    psx_mod_write_word(q + 4u, 0x2F808080u);
    psx_mod_write_word(q + 8u, ya | x0);
    psx_mod_write_word(q + 12u, (clut << 16) | (v << 8) | u);
    psx_mod_write_word(q + 16u, ya | x1);
    psx_mod_write_word(q + 20u, (tp << 16) | (v << 8) | u1);
    psx_mod_write_word(q + 24u, yb | x0);
    psx_mod_write_word(q + 28u, (v1 << 8) | u);
    psx_mod_write_word(q + 32u, yb | x1);
    psx_mod_write_word(q + 36u, (v1 << 8) | u1);
    psx_mod_write_word(slot, (psx_mod_read_word(slot) & 0xFF000000u) | (q & 0x00FFFFFFu));
}

static void alundra_widescreen_activate(void) {
    if (!psx_mod_set_fixed_display_aspect(16, 9)) {
        fprintf(stdout, "alundra: widescreen: runtime rejected 16:9 display aspect\n");
        return;
    }
    g_alundra_xtab = psx_mod_alloc_guest_memory(ALU_TABLE_LEN * 2u, 4u);
    g_alundra_edge = psx_mod_alloc_gpu_dma_memory(256u, 16u);
    g_alundra_strip = psx_mod_alloc_gpu_dma_memory(64u, 16u);
    g_alundra_mask = psx_mod_alloc_gpu_dma_memory(ALU_MASK_QUADS * ALU_MASK_QUAD_SZ, 16u);
#if ALU_CLOUD_WIDEN
    g_alundra_cloud = psx_mod_alloc_gpu_dma_memory(
        ALU_CLOUD_LAYERS * ALU_CLOUD_LAYER_SZ, 16u);
    if (g_alundra_cloud) {
        /* Packet header: 3 words; cmd 0x7F (16x16 sprite, semi-trans, raw). */
        for (uint32_t i = 0; i < ALU_CLOUD_LAYERS * ALU_CLOUD_COLS * ALU_CLOUD_ROWS; i++) {
            psx_mod_write_word(g_alundra_cloud + i * ALU_CLOUD_PKT, 0x03000000u);
            psx_mod_write_word(g_alundra_cloud + i * ALU_CLOUD_PKT + 4u, 0x7F000000u);
        }
    }
#endif
}

PSX_MOD_CONSTRUCTOR(alundra_register_widescreen_plugin) {
    (void)psx_mod_register_activation_plugin("alundra.widescreen",
                                             alundra_widescreen_activate);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CED0u,
                                              0xAFB80018u, alundra_cols_15);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CF04u,
                                              0xAFB80018u, alundra_cols_14);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CEF0u,
                                              0xAFB60010u, alundra_first_col);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D14Cu,
                                              0x2609000Du, alundra_use_xtab);
#if ALU_EDGE_TILE_SHADE
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D220u,
                                              0x25290014u, alundra_edge_tile_a);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D390u,
                                              0x24A50014u, alundra_edge_tile_b);
#endif
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D444u,
                                              0x00007021u, alundra_after_tiles);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x800262ECu,
                                              0x8FBF008Cu, alundra_cloud_strip);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005D5E0u,
                                              0x03E00008u, alundra_mask_stretch);
#if ALU_CLOUD_WIDEN
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005C538u,
                                              0x01B87023u, alundra_cloud_row_start);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005C5F4u,
                                              0x1440FFD2u, alundra_cloud_loop);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005C4E8u,
                                              0x0120F021u, alundra_cloud_base);
#endif
}
