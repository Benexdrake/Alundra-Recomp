/*
 * Alundra HUD layout enhancement (feature "alundra.hud", switchable).
 *
 * In 16:9 the game still draws its HUD inside the 4:3 area. This feature moves
 * the three HUD groups to the corners of the wide view:
 *   - item slots   -> bottom left
 *   - hearts + max -> top left
 *   - gilder count -> bottom right
 *
 * The HUD packets live in a static GPU buffer (0x80175D00..0x80176700, two
 * parities) that the game rewrites with absolute positions every frame. Pre hook
 * (start of the HUD build) undoes last frame's shift, post hook (after the HUD
 * functions) scans the buffer and shifts the packets again.
 */
#include "mod_plugins.h"
#include "cpu_state.h"

#define ALU_HUD_LO      0x80175D00u
#define ALU_HUD_HI      0x80176700u
#define ALU_HUD_MAXREC  96
#define ALU_HUD_BOTTOM_ITEMS  176 /* item slots: y 0x10 -> 192 (bottom margin 16) */
#define ALU_HUD_BOTTOM_GILDER 192 /* gilder digits: y 0x10 -> 208 */
#define ALU_HUD_STONE_Y       0   /* big 10-HP stone: screen y (hearts start at 16) */
#define ALU_HUD_HEART_SHIFT   104 /* hearts start at x=0x78, move to x=0x10 */

typedef struct {
    uint32_t addr;
    int16_t dx, dy;
    int16_t nx, ny; /* vertex 0 right after our shift */
    uint8_t verts;
    uint8_t valid;
    uint32_t tex; /* word at +12 (u, v, clut) when shifted */
} AluHudRec;

/* Persistent per-address shift table (one entry per 4-byte slot of the HUD
 * buffer). Never cleared when a shift is undone: some packets (the "/10"
 * glyphs) get their position written only once by the game, so a lost undo
 * would otherwise be taken as the game's own value and be shifted again
 * (double shift, glyphs ended up at the item slots). Undo is value-exact and
 * also checks the texture word so a game rewrite is never "undone". */
#define ALU_HUD_SLOTS ((ALU_HUD_HI - ALU_HUD_LO) / 4u)
static AluHudRec g_hud_rec[ALU_HUD_SLOTS];

static void alundra_hud_move(uint32_t addr, uint8_t verts, int dx, int dy) {
    for (uint8_t k = 0; k < verts; k++) {
        uint32_t p = addr + 8u + 8u * k;
        psx_mod_write_half(p, (uint16_t)((int16_t)psx_mod_read_half(p) + dx));
        psx_mod_write_half(p + 2u, (uint16_t)((int16_t)psx_mod_read_half(p + 2u) + dy));
    }
}

/* Before the HUD is rebuilt: restore positions we shifted last frame. Only if
 * vertex 0 still holds our shifted value; otherwise the game rewrote the packet
 * itself and it is already in original coordinates. */
static void alundra_hud_pre(struct CPUState* cpu, uint32_t address) {
    (void)cpu; (void)address;
    for (uint32_t i = 0; i < ALU_HUD_SLOTS; i++) {
        AluHudRec* r = &g_hud_rec[i];
        if (!r->valid) continue;
        if ((int16_t)psx_mod_read_half(r->addr + 8u) == r->nx &&
            (int16_t)psx_mod_read_half(r->addr + 10u) == r->ny &&
            psx_mod_read_word(r->addr + 12u) == r->tex)
            alundra_hud_move(r->addr, r->verts, -r->dx, -r->dy);
        /* Entry stays valid: once undone the packet no longer holds the shifted
         * value, so repeating the undo is a no-op, but a lost undo is caught
         * on the next pass. */
    }
}

static void alundra_hud_post(struct CPUState* cpu, uint32_t address) {
    (void)cpu; (void)address;
    int32_t m = psx_mod_widescreen_view_x_margin();
    if (m <= 0) return;
    alundra_hud_pre(cpu, address); /* undo any shift still pending */
    /* The game slides the HUD up out of view when the start menu opens. While
     * the HUD base y is not at rest (0x10), hide it completely so the original
     * sliding copy and our corner copy never show at the same time. */
    int hidden = (int16_t)psx_mod_read_half(0x800A825Au) != 0x10;
    uint32_t a = ALU_HUD_LO;
    while (a < ALU_HUD_HI) {
        uint32_t tag = psx_mod_read_word(a);
        uint32_t n = tag >> 24;
        uint32_t nxt = tag & 0x00FFFFFFu;
        uint32_t cmd = psx_mod_read_word(a + 4u) >> 24;
        uint8_t verts = 0;
        if (n == 4u && (cmd == 0x64u || cmd == 0x65u)) verts = 1;
        else if (n == 8u && cmd == 0x3Bu) verts = 4;
        if (verts && (nxt == 0xFFFFFFu || nxt < 0x200000u)) {
            int32_t x = (int16_t)psx_mod_read_half(a + 8u);
            int32_t y = (int16_t)psx_mod_read_half(a + 10u);
            /* Big stone (10 HP): texture u=96 v=40, clut 31186. Before the first menu
             * the game writes it at (51,192) instead of (104,16), so it is recognised
             * by texture, not by position. */
            /* The clut varies with game progress/HP state (grey stone = other clut),
             * so only u, v and the 16x16 size are matched. */
            int stone = verts == 1 && x < 0xF0 && cmd == 0x65u &&
                        ((psx_mod_read_word(a + 12u) & 0xFFFFu) == ((40u << 8) | 96u) ||   /* orange */
                         (psx_mod_read_word(a + 12u) & 0xFFFFu) == ((40u << 8) | 144u)) && /* grey */
                        psx_mod_read_word(a + 16u) == 0x00100010u;
            if (stone ? (y >= -64 && y < 0xF0 && x >= -64) : (y >= (hidden ? -64 : 0) && y < 0x40 && x >= 0 && x < 0x140)) {
                int dx, dy;
                if (hidden)         { dx = 0;                           dy = -400; }
                else if (verts == 1 && x < 0xF0 && (psx_mod_read_word(a + 12u) >> 16) == 31058u) {
                    /* "/N" max-heart glyphs (font clut 31058): not shown any more. */
                    dx = 0; dy = -400;
                }
                else if (stone) {
                    /* Big stone (= 10 HP, texture u=96 v=40 clut 31186; recognised by texture
                     * because before the first menu the game writes it at another x, like
                     * the "/10" glyphs): above the small hearts,
                     * top left (hearts start at screen x=16, y=16). */
                    dx = 16 - x - m;  dy = ALU_HUD_STONE_Y - y;
                }
                else if (verts == 1 && x < 8 && y < 0x20) {
                    /* Before the first menu the game writes the 6th heart column at x=3,
                     * already the final spot. Leave it. */
                    dx = 0; dy = 0;
                }
                else if (x < 0x70)  { dx = -m;                          dy = ALU_HUD_BOTTOM_ITEMS; }
                else if (x < 0xF0)  { dx = -m - ALU_HUD_HEART_SHIFT;    dy = 0; }
                else                { dx = m;                           dy = ALU_HUD_BOTTOM_GILDER; }
                AluHudRec* r = &g_hud_rec[(a - ALU_HUD_LO) >> 2];
                r->addr = a;
                r->dx = (int16_t)dx;
                r->dy = (int16_t)dy;
                r->verts = verts;
                r->tex = psx_mod_read_word(a + 12u);
                alundra_hud_move(a, verts, dx, dy);
                r->nx = (int16_t)psx_mod_read_half(a + 8u);
                r->ny = (int16_t)psx_mod_read_half(a + 10u);
                r->valid = 1;
            }
            a += 4u + 4u * n;
        } else {
            a += 4u;
        }
    }
}

PSX_MOD_CONSTRUCTOR(alundra_register_hud_plugin) {
    (void)psx_mod_register_instruction_plugin("alundra.hud", 0x8004C0C0u,
                                              0x00003021u, alundra_hud_pre);
    (void)psx_mod_register_instruction_plugin("alundra.hud", 0x8004C220u,
                                              0x8E03827Cu, alundra_hud_post);
}
