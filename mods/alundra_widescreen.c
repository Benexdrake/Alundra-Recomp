/*
 * Alundra Widescreen (Feature "widescreen", Plugin "alundra.widescreen").
 *
 * Neuaufbau, Phase 2: reiner Widescreen-Mod. Nur was der Breite dient:
 * 16:9-Aktivierung, Kachelschleife (21 Spalten), Kachelpuffer-Arena,
 * Wolken/Schatten-Overlay, Speicherraum-Nebel, Ringmaske, Tor-Schatten.
 * Keine Randfüllung, keine Raumfarben (Phase 3-5, siehe
 * mods/alundra_widescreen_legacy.c.txt für den Alt-Stand).
 *
 * Alundra is a 2D tile game. Its background tile pass (guest 0x8002CE..0x8002D440)
 * draws 15 columns of 24 px, positioned from a fixed X table in the main EXE
 * (0x800203F0). To fill a 16:9 view we draw more columns (EXTRA_COLS wider) and
 * start up to LEFT_COLS columns left of the camera, using a private X table.
 */
#include <stdio.h>
#include "alundra_common.h"
#include "cpu_state.h"
#include "gpu.h"

#define ALU_LEFT_COLS  3
#define ALU_EXTRA_COLS 6
#define ALU_TABLE_LEN  (15 + ALU_EXTRA_COLS + 2)

#define R_T7 15
#define R_T8 24
#define R_S6 22
#define R_A0 4
#define R_A2 6
#define R_A3 7
#define R_V0 2
#define R_T1 9
#define R_T0 8
#define R_T3 11
#define R_T6 14
#define R_FP 30

static uint32_t g_alundra_xtab;
static uint32_t g_alundra_tilebuf;

/* ---------------------------------------------------------------- Wolken */
/* Cloud/shadow overlay (0x8005C294): 16x16 sprite grid, 21 columns x 16 rows
 * per layer, packet buffer stride 5440. Widen to CLOUD_COLS columns starting
 * CLOUD_LEFT_COLS left of the camera, in a private GPU-DMA packet arena. */
#define ALU_CLOUD_LEFT_COLS 4
#define ALU_CLOUD_COLS      30
#define ALU_CLOUD_ROWS      16
#define ALU_CLOUD_LAYERS    4
#define ALU_CLOUD_PKT       16u
#define ALU_CLOUD_LAYER_SZ  (ALU_CLOUD_COLS * ALU_CLOUD_ROWS * ALU_CLOUD_PKT)
#define ALU_CLOUD_MAPW      40
#define ALU_CLOUD_WIDEN 1       /* 0 schaltet Arena und Hooks ab */
static uint32_t g_alundra_cloud;

/* Dunkelraum-Erkennung (Phase 5 entscheidet): pro Frame gezählte Wolkenzellen
 * am linken/rechten Rand (colL x=0, outL x<0, colR x=304, outR x>=320).
 * Der Quad-Bau fehlt bewusst; Abholen per alundra_cloud_edge_counts_take(). */
static uint32_t g_cl_s2;
static uint32_t g_cl_hide;
extern int alundra_room_hide_clouds(void); /* alundra_bgcolor.c */
static uint32_t g_cl_now[4];

void alundra_cloud_edge_counts_take(uint32_t out[4]) {
    for (int i = 0; i < 4; i++) { out[i] = g_cl_now[i]; g_cl_now[i] = 0; }
}

/* SUBU t6,t5,t8 at row start: begin left of column 0 and wrap the map column. */
static void alundra_cloud_row_start(struct CPUState* cpu, uint32_t address) {
    (void)address;
    g_cl_s2 = cpu->gpr[18];
    if (cpu->gpr[13] == 0) {        /* erste Zeile des Frames: Raum pruefen */
        g_cl_hide = alundra_room_hide_clouds();   /* Wolken ausserhalb 4:3 ausblenden */
    }
    int32_t a3 = (int32_t)cpu->gpr[R_A3] - ALU_CLOUD_LEFT_COLS;
    while (a3 < 0) a3 += ALU_CLOUD_MAPW;
    cpu->gpr[R_A3] = (uint32_t)a3;
    cpu->gpr[R_T1] = (uint32_t)(-(int32_t)ALU_CLOUD_LEFT_COLS * 16);
}

/* BNE v0,zero (loop back): continue until all columns are drawn. */
static void alundra_cloud_loop(struct CPUState* cpu, uint32_t address) {
    (void)address;
    int32_t t1 = (int32_t)cpu->gpr[R_T1];
    uint32_t s2 = cpu->gpr[18];
    if (s2 != g_cl_s2) {            /* this cell emitted a packet; its x = t1 - 16 */
        int32_t x = t1 - 16;
        if (g_cl_hide)                          /* Raum-Switch (alundra_bgcolor.c): Wolken komplett aus */
            psx_mod_write_half(cpu->gpr[R_T0] - 16u + 10u, 0x0300u);
        if (x == 0) g_cl_now[0]++;
        else if (x < 0) g_cl_now[1]++;
        else if (x == 304) g_cl_now[2]++;
        else if (x >= 320) g_cl_now[3]++;
    }
    g_cl_s2 = s2;
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

/* ---------------------------------------------------------------- Kacheln */
/* Map is 52 columns wide (camera X max 927 = 52*24 - 321). Reading past it hits
 * the next row's data and draws garbage, so clamp the column count to the map end.
 * (Fest 52; in Phase 3 prüfen, ob das Limit ganz fallen kann.) */
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

/* Tile packet buffers: the game gives each display parity 12000 B (600 tile packets, 20 B) followed by
 * a 1600 B pool of 8-byte E1 texpage packets (s1 = s0 + 12000), with no bounds check. 4:3 needs
 * up to ~550 packets; 21 columns x 2 tile layers need more, so the buffer overran into the E1 pool and then
 * the game variables behind it (camera, palette table...), which the GPU walker then executed as
 * commands (VRAM copy -> corrupted tile pages). Both parities are moved into a private arena. */
#define ALU_TILEBUF_TILES   1500u
#define ALU_TILEBUF_E1      800u
#define ALU_TILEBUF_TILE_B  (ALU_TILEBUF_TILES * 20u)
#define ALU_TILEBUF_STRIDE  (ALU_TILEBUF_TILE_B + ALU_TILEBUF_E1 * 8u + 32u)

static void alundra_tilebuf_init(uint32_t base) {
    /* The game pre-initialises its tile packets (n=4, 0x65 808080, 24x16); the draw loop only
     * writes x/y/u/v/clut and the link. Same template for the private buffers. */
    for (uint32_t i = 0; i < ALU_TILEBUF_TILES; i++) {
        uint32_t pk = base + i * 20u;
        psx_mod_write_word(pk, 0x04000000u);
        psx_mod_write_word(pk + 4u, 0x65808080u);
        psx_mod_write_word(pk + 16u, 0x00100018u);
    }
}

/* ADDU s1,v0,v1 at 0x8002CFD8: v0 = 13600 * parity, s0 = 0x800DD878-based buffer already set.
 * Make s0 = arena base of this parity and s1 = base + tile bytes (E1 pool start). */
static void alundra_tilebuf(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alundra_tilebuf) return;
    uint32_t base = g_alundra_tilebuf + (cpu->gpr[2] ? ALU_TILEBUF_STRIDE : 0u);
    /* Arena is cleared after activation: write the template lazily as well. */
    if (psx_mod_read_word(base + 4u) != 0x65808080u) alundra_tilebuf_init(base);
    cpu->gpr[16] = base;
    cpu->gpr[2] = base;
    cpu->gpr[3] = ALU_TILEBUF_TILE_B;
}


/* ---- US (SLUS-00553) Varianten der Kachelschleife (Branch usa-slus-00553) ----
 * Spaltenzahl liegt in s3 (-> SW s3,32($sp)), X-Tabelle in t6 (0x800203F0),
 * Schleifenende vergleicht t6 mit 0x800203F0 + 2*Spalten. */
#define R_S3 19
#define ALU_US_XTAB_ORIG 0x800203F0u
static void alundra_us_cols_15(struct CPUState* cpu, uint32_t address) {
    (void)address;
    cpu->gpr[R_S3] = alundra_clamped_cols(cpu, 15);
}
static void alundra_us_cols_14(struct CPUState* cpu, uint32_t address) {
    (void)address;
    cpu->gpr[R_S3] = alundra_clamped_cols(cpu, 14);
}
static void alundra_us_use_xtab(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (g_alundra_xtab) cpu->gpr[R_T6] = g_alundra_xtab;
}
/* SLT v0,t6,v0 (v0 = Original-Tabelle + 2*Spalten): Grenze auf die private Tabelle umlegen. */
static void alundra_us_xtab_end(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (g_alundra_xtab)
        cpu->gpr[R_V0] = g_alundra_xtab + (cpu->gpr[R_V0] - ALU_US_XTAB_ORIG);
}


#if ALU_CLOUD_WIDEN
/* ---- US: Wolken/Schatten-Overlay (Routine ~0x8005E488), Zellen 16x16, 21 -> 30 Spalten ----
 * s2 = Layer-Index, t0 = Paketbasis, a3 = Karten-Spalte, t1 = x (Start 0, Ende t1<336). */
static void alundra_us_cloud_base(struct CPUState* cpu, uint32_t address) { /* ADDU s6,t1,zero */
    (void)address;
    if (!g_alundra_cloud) return;
    uint32_t layer = cpu->gpr[18];
    if (layer >= ALU_CLOUD_LAYERS) layer = 0;
    cpu->gpr[R_T0] = g_alundra_cloud + layer * ALU_CLOUD_LAYER_SZ;
}
static void alundra_us_cloud_row(struct CPUState* cpu, uint32_t address) { /* SUBU t6,t5,t9 */
    (void)address;
    if (!g_alundra_cloud) return;
    int32_t a3 = (int32_t)cpu->gpr[R_A3] - ALU_CLOUD_LEFT_COLS;
    while (a3 < 0) a3 += ALU_CLOUD_MAPW;
    cpu->gpr[R_A3] = (uint32_t)a3;
    cpu->gpr[R_T1] = (uint32_t)(-(int32_t)ALU_CLOUD_LEFT_COLS * 16);
}
static void alundra_us_cloud_loop(struct CPUState* cpu, uint32_t address) { /* BNE v0,zero */
    (void)address;
    {   /* Randzaehlung fuer die Dunkelraum-Erkennung (s1 = Zahl emittierter Zellen) */
        static uint32_t prev_s1;
        uint32_t s1 = cpu->gpr[17];
        if (s1 != prev_s1) {
            int32_t x = (int32_t)cpu->gpr[R_T1] - 16;
            if (x == 0) g_cl_now[0]++;
            else if (x < 0) g_cl_now[1]++;
            else if (x == 304) g_cl_now[2]++;
            else if (x >= 320) g_cl_now[3]++;
        }
        prev_s1 = s1;
    }
    if (!g_alundra_cloud) return;
    cpu->gpr[R_V0] = ((int32_t)cpu->gpr[R_T1] < (ALU_CLOUD_COLS - ALU_CLOUD_LEFT_COLS) * 16) ? 1u : 0u;
}
#endif

/* ---------------------------------------------------------- Speicher-Nebel */
/* Save/load room cloud layer: six 64x128 semi-trans sprite strips (cmd 0x66, texture period
 * 320 px = 5 strips) drawn through the generic object draw function in the main EXE. The
 * six strips only cover the 4:3 view, so for 16:9 we add shifted +-320 px copies (same u/v
 * and texpage, which repeat with period 320) of the strip that continues the row at each end.
 * Hooked right after the function's last OT link (0x800262EC); fp = object, s0 = sprite
 * packet, s1 = its E1 texpage packet, OT base pointer at 0x8015AE90, slot = fp.s16[4]*4+0x70. */
#define ALU_STRIP_OT_PTR 0x801F48BCu /* US (DE: 0x8015AE90) */
#define ALU_STRIP_PERIOD 320
static uint32_t g_alundra_strip; /* 2 x 32 B: E1 packet (8 B) + sprite packet (24 B) */
uint32_t g_alu_fog_tick;         /* >0: Nebelszene aktiv (gelesen in alundra_bgcolor.c) */
extern void alundra_fog_room_edges(uint32_t ot);

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
    g_alu_fog_tick = 4u; /* Nebelszene: Hintergrundfarbe (alundra_bgcolor.c) */
    /* Seitenrand-Verlauf (alundra_fog_room_edges(ot + 0x70u), einmal pro Frame beim linkesten Streifen)
     * vom Nutzer abgelehnt (2026-11-25); Funktion bleibt in alundra_edgefade.c. */
    uint32_t slot = ot + (uint32_t)((int32_t)(int16_t)psx_mod_read_half(fp + 4u) * 4) + 0x70u;
    /* leftmost strip x0 is in [-64, 0) (the game wraps it at -64); strip j=1 (0 <= x < 64)
     * continues past the right end, strip j=4 (192 < x < 256) continues past the left end. */
    if (x >= 0 && x < 64 && x + ALU_STRIP_PERIOD < 320 + m)
        alundra_strip_emit(slot, s0, s1, x + ALU_STRIP_PERIOD, g_alundra_strip + 32u);
    if (x > 192 && x < 256 && x - ALU_STRIP_PERIOD + 64 > -m)
        alundra_strip_emit(slot, s0, s1, x - ALU_STRIP_PERIOD, g_alundra_strip);
}

/* ------------------------------------------------------------- Ringmaske */
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


/* US: Ringmaske. Streifen-Routine 0x8005F5C8..0x8005F784 (JR ra): a2 = Sprite, t4 = OT-Slot-Pointer.
 * Texpage fest 0x3D (nur die Maske mit CLUT 0x7C10 kommt hier vor; DE liest sie aus einer Tabelle). */
static void alundra_us_mask_stretch(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alundra_mask) return;
    uint32_t spr = cpu->gpr[R_A2], slot = cpu->gpr[12];
    if ((spr & 0xFF000000u) != 0x80000000u || (slot & 0xFF000000u) != 0x80000000u) return;
    if ((psx_mod_read_word(spr + 4u) >> 24) != 0x67u) return;
    uint32_t wh = psx_mod_read_word(spr + 16u);
    uint32_t w = wh & 0xFFFFu, h = wh >> 16;
    if (h != 4u || w == 0u || w > 256u) return;
    if ((psx_mod_read_word(slot) & 0x00FFFFFFu) != (spr & 0x00FFFFFFu)) return;
    uint32_t tp = 0x3Du;
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

/* ------------------------------------------------------------ Tor-Schatten */
/* Gate shadow: under a gate the game switches the player's shadow slot from the normal part
 * definition to a second one that is drawn at a fixed height (a black blob at the gate foot).
 * Hook in the generic part-sprite routine (0x8002DC28..), just before the packet is linked into the
 * OT (LW v1,0(t2)): t2 = packet tag, t6 = next part (already +14), a2 = OT slot pointer.
 * The slot pointers that carry the normal player shadow are learned; the same slot with the other
 * definition gets its four vertices collapsed (nothing is drawn). */
#define ALU_SHADOW_T6_PLAYER 0x800CC928u /* US;DE 0x800E4D30; normal player shadow definition (+14 after advance) */
#define ALU_SHADOW_T6_GATE   0x800CC948u /* US;DE 0x800E4D50; alternate definition used under gates */
static uint32_t g_alundra_player_slot[2];
static void alundra_shadow_part(struct CPUState* cpu, uint32_t address) {
    (void)address;
    uint32_t pkt = cpu->gpr[10] + 4u; /* t2 + 4 = command word */
    if ((pkt & 0xFF000000u) != 0x80000000u) return;
    if ((psx_mod_read_word(pkt) >> 24) != 0x2Fu || psx_mod_read_word(pkt + 8u) != 0x780C5898u) return;
    uint32_t t6 = cpu->gpr[R_T6], slot = cpu->gpr[R_A2];
    if (t6 == ALU_SHADOW_T6_PLAYER) {
        if (slot != g_alundra_player_slot[0] && slot != g_alundra_player_slot[1]) {
            g_alundra_player_slot[1] = g_alundra_player_slot[0];
            g_alundra_player_slot[0] = slot;
        }
        return;
    }
    if (t6 == ALU_SHADOW_T6_GATE &&
        (slot == g_alundra_player_slot[0] || slot == g_alundra_player_slot[1])) {
        for (uint32_t o = 6u; o <= 30u; o += 8u) psx_mod_write_half(pkt + o, 0xFE00u);
    }
}

/* -------------------------------------------------------------- Aktivierung */
static void alundra_widescreen_activate(void) {
    if (!psx_mod_set_fixed_display_aspect(16, 9)) {
        fprintf(stdout, "alundra: widescreen: runtime rejected 16:9 display aspect\n");
        return;
    }
    g_alundra_xtab = psx_mod_alloc_guest_memory(ALU_TABLE_LEN * 2u, 4u);
    g_alundra_tilebuf = psx_mod_alloc_gpu_dma_memory(2u * ALU_TILEBUF_STRIDE, 16u);
    if (g_alundra_tilebuf) {
        for (uint32_t par = 0; par < 2u; par++)
            alundra_tilebuf_init(g_alundra_tilebuf + par * ALU_TILEBUF_STRIDE);
    }
    g_alundra_strip = psx_mod_alloc_gpu_dma_memory(128u, 16u);
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
    /* US-Sites (SLUS-00553) */
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D1CCu,
                                              0xAFB30020u, alundra_us_cols_15);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D204u,
                                              0xAFB30020u, alundra_us_cols_14);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D1ECu,
                                              0xAFB60010u, alundra_first_col);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D2F0u,
                                              0x00438821u, alundra_tilebuf);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D4B0u,
                                              0x260A000Du, alundra_us_use_xtab);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D778u,
                                              0x01C2102Au, alundra_us_xtab_end);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005F784u,
                                              0x03E00008u, alundra_us_mask_stretch);
#if ALU_CLOUD_WIDEN
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005E6B0u,
                                              0x0120B021u, alundra_us_cloud_base);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005E6F8u,
                                              0x01B97023u, alundra_us_cloud_row);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005E7B8u,
                                              0x1440FFD1u, alundra_us_cloud_loop);
#endif
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CED0u,
                                              0xAFB80018u, alundra_cols_15);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CF04u,
                                              0xAFB80018u, alundra_cols_14);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CEF0u,
                                              0xAFB60010u, alundra_first_col);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002CFD8u,
                                              0x00438821u, alundra_tilebuf);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002D14Cu,
                                              0x2609000Du, alundra_use_xtab);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x800261A4u,
                                              0x8FBF00CCu, alundra_cloud_strip); /* US */
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005D5E0u,
                                              0x03E00008u, alundra_mask_stretch);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8002E150u,
                                              0x8D430000u, alundra_shadow_part);
#if ALU_CLOUD_WIDEN
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005C538u,
                                              0x01B87023u, alundra_cloud_row_start);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005C5F4u,
                                              0x1440FFD2u, alundra_cloud_loop);
    (void)psx_mod_register_instruction_plugin("alundra.widescreen", 0x8005C4E8u,
                                              0x0120F021u, alundra_cloud_base);
#endif
}
