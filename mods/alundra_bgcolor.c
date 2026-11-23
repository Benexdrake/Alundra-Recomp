/*
 * Alundra Hintergrundfarbe (Feature "background_color", Plugin "alundra.bgcolor").
 * Phase 3: Das Clear-FillRect (Befehl 0x02, 320x240, erstes Paket jedes Frames) wird pro Raum
 * umgefaerbt. Das Spiel baut das Paket jedes Frame neu, indem eine Schleife (0x80086CC4,
 * SW a1,0(a0)) die Vorlage aus der DRAWENV-Struktur (s0 = 0x8013FAE8 / 0x8013FA64) wortweise
 * in den OT-Ende-Block kopiert. Der Hook ersetzt das Farbwort direkt vor dem Schreiben.
 * Nur im 16:9-Betrieb aktiv (margin > 0); Default (8,0,0), pro Raum per Switch ueberschreibbar.
 */
#include "alundra_common.h"
#include "cpu_state.h"

#define ALU_BG_SITE 0x80086CC4u
#define ALU_BG_WORD 0xAC850000u /* SW $a1, 0($a0) */

/* Farbformat 0x00BBGGRR. Hash: alundra_room_sig(). */
static uint32_t alundra_room_color(uint32_t sig) {
    switch (sig) {
    case 0x793D39CFu: /* Skelett-Raeume: Spiel-Clear (0,8,0) */
    case 0x1B8E3013u:
        return 0x00000800u;
    /* Mine (Phase 5): Die Wolken-/Schattenschicht dunkelt nur den 4:3-Teil ab. Alt-Mod: Aussenfuellung
     * (8,8,0) minus ~40 % Schwarz ergab praktisch (0,0,0). Der abgedunkelte Aussenton ist also Schwarz;
     * ohne Fuellquad zeigt der Rand nur das Clear. Messung/Sichtpruefung durch den Nutzer offen
     * (Raum 3: (8,8,0)-Kacheln gegen (0,0,0)-Leerspalten, siehe phase-5). */
    case 0x877DDF27u:
    case 0xD78EE04Cu:
    case 0x56AA69AFu:
        return 0x00000000u;
    default: /* Haeuser u. a.: Leere im Raum misst (R,G,B)=(8,0,0), Rand ausserhalb 4:3 war sonst (0,0,0) */
        return 0x00000008u;
    }
}

static void alundra_bgcolor_hook(struct CPUState* cpu, uint32_t address) {
    (void)address;
    uint32_t w = cpu->gpr[5];
    if ((w >> 24) != 0x02u) return;
    /* nur das Vollbild-Clear: Wortfolge 02BBGGRR, xy, 0x00F00140 (240 x 320); a3 zeigt bereits aufs xy-Wort */
    if (psx_mod_read_word(cpu->gpr[7] + 4u) != 0x00F00140u) return;
    if (psx_mod_widescreen_view_x_margin() <= 0) return;
    uint32_t sig = alundra_room_sig();
    if (sig == 0) return; /* keine Karte geladen (Laden/Menue): Spiel-Clear lassen */
    cpu->gpr[5] = 0x02000000u | (alundra_room_color(sig) & 0x00FFFFFFu);
}

static void alundra_bgcolor_activate(void) {}

PSX_MOD_CONSTRUCTOR(alundra_register_bgcolor_plugin) {
    (void)psx_mod_register_activation_plugin("alundra.bgcolor", alundra_bgcolor_activate);
    (void)psx_mod_register_instruction_plugin("alundra.bgcolor", ALU_BG_SITE, ALU_BG_WORD, alundra_bgcolor_hook);
}
