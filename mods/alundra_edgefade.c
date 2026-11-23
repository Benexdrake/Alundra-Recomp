/*
 * Alundra weicher Kartenrand (Feature "map_edge_fade", Plugin "alundra.edgefade").
 * Phase 4: pro Kartenseite ein Gouraud-Semitrans-Quad (cmd 0x3A, E1 0xE1000040 = subtraktiv B-F),
 * Farbe `dark` am Rand -> 0 nach innen, y 0..240. Wird nach der Kachelschleife (Site 0x8002D444)
 * in OT-Slot 959 head-eingefuegt (ueber Welt, unter Wolken und HUD). Keine Heuristik: nur Raeume
 * aus der Tabelle bekommen einen Verlauf, alle anderen nichts. Nur im 16:9-Betrieb (margin > 0).
 */
#include "alundra_common.h"
#include "cpu_state.h"

#define ALU_EDGE_SITE 0x8002D444u
#define ALU_EDGE_WORD 0x00007021u
#define ALU_EDGE_SLOT 959u
#define ALU_EDGE_WORDS 10u /* Tag + E1 + 4 * (Farbe, Vertex) */

typedef struct {
    uint32_t sig;   /* alundra_room_sig(), 0 = nicht benutzen */
    uint32_t bsig;  /* alundra_room_border_sig() (stabil ueber Spielstaende), 0 = nicht benutzen */
    int32_t  w_l;   /* Verlaufsbreite links in px (0 = keine) */
    int32_t  w_r;   /* rechts */
    int32_t  cols;  /* Kartenspaltenzahl (Breite = cols * 24 px) */
    uint32_t dark;  /* Grauwert (0..255), der am Rand subtrahiert wird; 0xFF = bis Schwarz */
} AluEdgeRoom;

static const AluEdgeRoom g_alu_edge_rooms[] = {
    { 0x0B440C2Du, 0, 12, 28, 52, 0xFFu }, /* Dorf */
    { 0xB4655671u, 0, 12, 28, 52, 0xFFu }, /* Dorf, anderer Kartenzustand */
    { 0, 0x1D39BE10u, 12, 28, 52, 0xFFu }, /* Dorf ueber Randsignatur (Hash 0x498E8C2D im Spielstand 2026-11-22) */
    { 0x4F8B40B1u, 0xDBBD8DE9u, 12, 12, 52, 0xFFu }, /* Aussenmap Wald nahe Treppe/Mauer (Kamera X=0 gemessen) */
};

static uint32_t g_alu_edge_arena; /* 4 Pakete a 64 B */

static void alundra_quad_build_e1(uint32_t pkt, uint32_t e1, int32_t xl, int32_t xr, uint32_t cl, uint32_t cr) {
    uint32_t yt = 0u, yb = 240u;
    psx_mod_write_word(pkt + 4u, e1);
    psx_mod_write_word(pkt + 8u, cl | 0x3A000000u);
    psx_mod_write_word(pkt + 12u, (yt << 16) | ((uint32_t)xl & 0xFFFFu));
    psx_mod_write_word(pkt + 16u, cr);
    psx_mod_write_word(pkt + 20u, (yt << 16) | ((uint32_t)xr & 0xFFFFu));
    psx_mod_write_word(pkt + 24u, cl);
    psx_mod_write_word(pkt + 28u, (yb << 16) | ((uint32_t)xl & 0xFFFFu));
    psx_mod_write_word(pkt + 32u, cr);
    psx_mod_write_word(pkt + 36u, (yb << 16) | ((uint32_t)xr & 0xFFFFu));
}

static void alundra_quad_build(uint32_t pkt, int32_t xl, int32_t xr, uint32_t cl, uint32_t cr) {
    alundra_quad_build_e1(pkt, 0xE1000040u, xl, xr, cl, cr);
}

/* Additiver Verlauf (B+F) in der Hintergrundfarbe: am Rand voll, nach innen 0. Hebt das Schwarz des
 * subtraktiven Verlaufs auf die Clear-Farbe (8,0,0) an, damit Rand und Hintergrund nahtlos sind. */
#define ALU_EDGE_ADD_COLOR 0x000008u
static void alundra_edge_add_build(uint32_t pkt, int32_t x_dark, int32_t x_clear) {
    int32_t xl = x_dark < x_clear ? x_dark : x_clear;
    int32_t xr = x_dark < x_clear ? x_clear : x_dark;
    alundra_quad_build_e1(pkt, 0xE1000020u, xl, xr, x_dark < x_clear ? ALU_EDGE_ADD_COLOR : 0u,
                          x_dark < x_clear ? 0u : ALU_EDGE_ADD_COLOR);
}

static void alundra_edge_build(uint32_t pkt, int32_t x_dark, int32_t x_clear, uint32_t dark) {
    int32_t xl = x_dark < x_clear ? x_dark : x_clear;
    int32_t xr = x_dark < x_clear ? x_clear : x_dark;
    alundra_quad_build(pkt, xl, xr, (x_dark < x_clear ? dark : 0u) * 0x010101u,
                       (x_dark < x_clear ? 0u : dark) * 0x010101u);
}

/* Mine-Raeume: die Schattenschicht des Spiels deckt nur das 4:3-Band (Bildschirm-X 0..320) ab.
 * Ausserhalb (-m..0 und 320..320+m) wird subtraktiv bis Schwarz abgedunkelt, mit je 8 px Rampe
 * ins Band. 4 Pakete a 64 B: 0 = links voll, 64 = links Rampe, 128 = rechts voll, 192 = rechts Rampe. */
#define ALU_BAND_RAMP 8
/* Erkennung ohne Raumliste: alundra_cloud_loop zaehlt pro Frame die Schattenzellen am Rand
 * (colL x=0, outL x<0, colR x=304, outR x>=320). Volle Randspalte (>= 12 von 16 Zeilen) und keine
 * Zelle ausserhalb = die Schattenschicht endet am 4:3-Rand -> aussen abdunkeln. */
extern void alundra_cloud_edge_counts_take(uint32_t out[4]);

static void alundra_edgefade_hook(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alu_edge_arena) return;
    int32_t m = psx_mod_widescreen_view_x_margin();
    if (m <= 0) return;
    uint32_t sig = alundra_room_sig();
    if (sig == 0) return;
    {   /* Schattenschicht-Rand, raumunabhaengig (vor der Raumtabelle, die ist nur fuer Kartenraender) */
        uint32_t c[4];
        alundra_cloud_edge_counts_take(c);
        int fill_l = c[0] >= 12 && c[1] == 0, fill_r = c[2] >= 12 && c[3] == 0;
        if (fill_l || fill_r) {
            uint32_t ot = psx_mod_read_word(cpu->gpr[29] + 0x58u);
            if ((ot & 0xFF000000u) == 0x80000000u) {
                uint32_t slot = ot + ALU_EDGE_SLOT * 4u;
                uint32_t head = psx_mod_read_word(slot);
                uint32_t next = head & 0x00FFFFFFu;
                const uint32_t F = 0x808080u; /* 50 % */
                if (fill_r) {
                    alundra_quad_build(g_alu_edge_arena + 192u, 320 - ALU_BAND_RAMP, 320, 0u, F);
                    alundra_quad_build(g_alu_edge_arena + 128u, 320, 320 + m, F, F);
                    for (uint32_t o = 192u; o >= 128u; o -= 64u) {
                        psx_mod_write_word(g_alu_edge_arena + o, ((ALU_EDGE_WORDS - 1u) << 24) | next);
                        next = (g_alu_edge_arena + o) & 0x00FFFFFFu;
                    }
                }
                if (fill_l) {
                    alundra_quad_build(g_alu_edge_arena + 64u, 0, ALU_BAND_RAMP, F, 0u);
                    alundra_quad_build(g_alu_edge_arena, -m, 0, F, F);
                    for (uint32_t o = 64u;; o -= 64u) {
                        psx_mod_write_word(g_alu_edge_arena + o, ((ALU_EDGE_WORDS - 1u) << 24) | next);
                        next = (g_alu_edge_arena + o) & 0x00FFFFFFu;
                        if (o == 0u) break;
                    }
                }
                psx_mod_write_word(slot, (head & 0xFF000000u) | next);
            }
        }
    }
    const AluEdgeRoom* room = 0;
    uint32_t bsig = alundra_room_border_sig();
    for (unsigned i = 0; i < sizeof g_alu_edge_rooms / sizeof g_alu_edge_rooms[0]; i++) {
        const AluEdgeRoom* r = &g_alu_edge_rooms[i];
        if ((r->sig && r->sig == sig) || (r->bsig && r->bsig == bsig)) { room = r; break; }
    }
    if (!room) return;

    uint32_t ot = psx_mod_read_word(cpu->gpr[29] + 0x58u);
    if ((ot & 0xFF000000u) != 0x80000000u) return;
    int32_t cam = (int32_t)psx_mod_read_word(ALU_CAM_X);
    int32_t xl = -cam, xr = room->cols * ALU_TILE_W - cam;

    uint32_t slot = ot + ALU_EDGE_SLOT * 4u;
    uint32_t head = psx_mod_read_word(slot);
    uint32_t next = head & 0x00FFFFFFu;

    /* Kette: links sub, links add, rechts sub, rechts add (Arena: 0, 256, 64, 320) */
    uint32_t pk[4], n = 0;
    if (room->w_l > 0 && xl + room->w_l > -m && xl < 320 + m) {
        alundra_edge_build(g_alu_edge_arena, xl, xl + room->w_l, room->dark);
        alundra_edge_add_build(g_alu_edge_arena + 256u, xl, xl + room->w_l);
        pk[n++] = g_alu_edge_arena;
        pk[n++] = g_alu_edge_arena + 256u;
    }
    if (room->w_r > 0 && xr - room->w_r < 320 + m && xr > -m) {
        alundra_edge_build(g_alu_edge_arena + 64u, xr, xr - room->w_r, room->dark);
        alundra_edge_add_build(g_alu_edge_arena + 320u, xr, xr - room->w_r);
        pk[n++] = g_alu_edge_arena + 64u;
        pk[n++] = g_alu_edge_arena + 320u;
    }
    if (!n) return;
    for (uint32_t i = n; i-- > 0;) {
        psx_mod_write_word(pk[i], ((ALU_EDGE_WORDS - 1u) << 24) | next);
        next = pk[i] & 0x00FFFFFFu;
    }
    psx_mod_write_word(slot, (head & 0xFF000000u) | next);
}

static void alundra_edgefade_activate(void) {
    g_alu_edge_arena = psx_mod_alloc_gpu_dma_memory(384u, 16u);
}

PSX_MOD_CONSTRUCTOR(alundra_register_edgefade_plugin) {
    (void)psx_mod_register_activation_plugin("alundra.edgefade", alundra_edgefade_activate);
    (void)psx_mod_register_instruction_plugin("alundra.edgefade", ALU_EDGE_SITE, ALU_EDGE_WORD, alundra_edgefade_hook);
}
