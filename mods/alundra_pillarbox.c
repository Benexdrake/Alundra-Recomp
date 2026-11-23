/*
 * Alundra schwarze Seitenbalken (Feature "pillarbox", Plugin "alundra.pillarbox").
 * In ausgewaehlten Szenen (Cutscenes, deren Raum breiter ist als 4:3 und am Rand fremde Karten
 * zeigt) werden links und rechts des 4:3-Bereichs deckend schwarze Quads gezeichnet.
 * Slot 959 = nach der Welt (Walk aufsteigend, Slot 5 wuerde vor der Welt gezeichnet), vor dem HUD-Block. Eigener E1 davor (kein Maskencheck).
 * Ausloeser: aktuell Raum-Signatur (alundra_room_sig / border_sig); Cutscene-Flag folgt, falls gefunden.
 * Nur im 16:9-Betrieb (margin > 0). Hook auf Site 0x8002D444 (nach der Kachelschleife).
 */
#include "alundra_common.h"
#include "cpu_state.h"

#define ALU_BOX_SITE 0x8002D444u
#define ALU_BOX_WORD 0x00007021u
#define ALU_BOX_SLOT 959u

typedef struct { uint32_t sig, bsig; } AluBoxRoom;
static const AluBoxRoom g_alu_box_rooms[] = {
    { 0xB3BDE1C5u, 0xE93D8AA1u }, /* Kars-/Melzas-Szene (Zauberer, Siegel) */
};

extern uint32_t g_alu_clear_rgb; /* alundra_bgcolor.c */
static uint32_t g_alu_box_arena; /* 2 Pakete a 32 B */

static void alundra_pillarbox_hook(struct CPUState* cpu, uint32_t address) {
    (void)address;
    if (!g_alu_box_arena) return;
    int32_t m = psx_mod_widescreen_view_x_margin();
    if (m <= 0) return;
    uint32_t sig = alundra_room_sig();
    if (sig == 0) return;
    uint32_t bsig = alundra_room_border_sig();
    /* 0x800A8250 war kein Live-Flag (blieb nach der Cutscene auf 1), daher nur die Raumliste. */
    int hit = 0;
    for (unsigned i = 0; i < sizeof g_alu_box_rooms / sizeof g_alu_box_rooms[0]; i++)
        if (g_alu_box_rooms[i].sig == sig || g_alu_box_rooms[i].bsig == bsig) hit = 1;
    if (!hit) return;
    uint32_t ot = psx_mod_read_word(cpu->gpr[29] + 0x58u);
    if ((ot & 0xFF000000u) != 0x80000000u) return;
    uint32_t slot = ot + ALU_BOX_SLOT * 4u;
    uint32_t head = psx_mod_read_word(slot);
    uint32_t next = head & 0x00FFFFFFu;
    for (int s = 1; s >= 0; s--) { /* Flat-Quad cmd 0x28, schwarz, deckend */
        uint32_t pkt = g_alu_box_arena + (uint32_t)s * 32u;
        int32_t x0 = s ? 320 : -m, x1 = s ? 320 + m : 0;
        psx_mod_write_word(pkt, (6u << 24) | next);
        psx_mod_write_word(pkt + 4u, 0xE1000200u);
        psx_mod_write_word(pkt + 8u, 0x28000000u | (g_alu_clear_rgb & 0x00FFFFFFu)); /* wie Raumhintergrund */
        psx_mod_write_word(pkt + 12u, (uint32_t)x0 & 0xFFFFu);
        psx_mod_write_word(pkt + 16u, (uint32_t)x1 & 0xFFFFu);
        psx_mod_write_word(pkt + 20u, (240u << 16) | ((uint32_t)x0 & 0xFFFFu));
        psx_mod_write_word(pkt + 24u, (240u << 16) | ((uint32_t)x1 & 0xFFFFu));
        next = pkt & 0x00FFFFFFu;
    }
    psx_mod_write_word(slot, (head & 0xFF000000u) | next);
}

static void alundra_pillarbox_activate(void) {
    g_alu_box_arena = psx_mod_alloc_gpu_dma_memory(64u, 16u);
}

PSX_MOD_CONSTRUCTOR(alundra_register_pillarbox_plugin) {
    (void)psx_mod_register_activation_plugin("alundra.pillarbox", alundra_pillarbox_activate);
    (void)psx_mod_register_instruction_plugin("alundra.pillarbox", ALU_BOX_SITE, ALU_BOX_WORD, alundra_pillarbox_hook);
}
