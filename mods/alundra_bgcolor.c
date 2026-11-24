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

#define ALU_BG_SITE 0x8008952Cu /* US; DE: 0x80086CC4 */
#define ALU_BG_WORD 0xAC850000u /* SW $a1, 0($a0) */

uint32_t g_alu_clear_rgb = 0x00000008u; /* zuletzt gezeichnete Clear-Farbe 0x00BBGGRR */

/* Die Spiel-Hintergruende kommen nur in diesen Kombinationen vor (Format 0x00BBGGRR, Anzeige (R,G,B)). */
#define ALU_BG_BLACK  0x00000000u /* (0,0,0) */
#define ALU_BG_RED    0x00000008u /* (8,0,0) Default */
#define ALU_BG_GREEN  0x00000800u /* (0,8,0) Spiel-Clear */
#define ALU_BG_YELLOW 0x00000808u /* (8,8,0) */
#define ALU_BG_GRAY   0x00080808u /* (8,8,8) */
#define ALU_BG_SKY    0x00887860u /* (96,120,136) Himmel */

/* Farbformat 0x00BBGGRR. Hash: alundra_room_sig(). Map-Liste: docs/Alundra/map-checkliste.md */
static uint32_t alundra_room_color(uint32_t sig) {
    if (alundra_room_border_sig() == 0x28BF794Du) return ALU_BG_GREEN; /* Hoehle, Gras abgeschnitten: Voll-Hash aendert sich */
    switch (sig) {
    case 0x793D39CFu: /* Skelett-Raeume: Spiel-Clear (0,8,0) */
    case 0x1B8E3013u:
    case 0x489CF39Au: /* Hoehle, Kamera 927,686 (2026-11-23): (0,8,0) */
    case 0xC114DD8Eu: /* Hoehle, Kamera 0,28 (2026-11-23): (0,8,0) */
    case 0x90C7456Eu: /* Raum 2026-11-23 (Hoehle, Kamera 0,0): (0,8,0) */
    case 0x5944BC3Cu: /* Gruft, dritte Map (2026-11-23): (0,8,0) */
    case 0xA5781E10u: /* Gruft, zweite Map (2026-11-23): (0,8,0) */
    case 0x8A1528CFu: /* Raum 2026-11-23 (dunkler Raum mit Treppe): Nutzer-Farbe (0,8,0) */
    case 0x32F8209Eu: /* Raum 2026-11-23 (Steinraum mit Buch): Spiel-Clear (0,8,0) */
        return ALU_BG_GREEN;
    /* Mine (Phase 5): Die Wolken-/Schattenschicht dunkelt nur den 4:3-Teil ab. Alt-Mod: Aussenfuellung
     * (8,8,0) minus ~40 % Schwarz ergab praktisch (0,0,0). Der abgedunkelte Aussenton ist also Schwarz;
     * ohne Fuellquad zeigt der Rand nur das Clear. Messung/Sichtpruefung durch den Nutzer offen
     * (Raum 3: (8,8,0)-Kacheln gegen (0,0,0)-Leerspalten, siehe phase-5). */
    case 0xD78EE04Cu:
    case 0x56AA69AFu:
        return ALU_BG_BLACK;
    case 0x09B8D459u: /* Aussenraum mit Himmel/Wolken rechts: Himmelblau (R,G,B)=(96,120,136) */
        return ALU_BG_SKY;
    case 0x877DDF27u: /* Mine-Problemraum: Nutzer-Farbe (8,8,0) */
    case 0xB2956EA2u: /* Raum 2026-11-23: Nutzer-Farbe (R,G,B)=(8,8,0) */
    case 0x7C900F86u: /* Raum 2026-11-23: Screenshot-Farbe (R,G,B)=(8,8,0) */
        return ALU_BG_YELLOW;
    case 0x0C30BD01u: /* Raum 2026-11-23: Nutzer-Farbe (R,G,B)=(8,8,8) */
        return ALU_BG_GRAY;
    default: /* Haeuser u. a.: Leere im Raum misst (R,G,B)=(8,0,0), Rand ausserhalb 4:3 war sonst (0,0,0) */
        return ALU_BG_RED;
    }
}

/* Raeume, in denen die Wolken-/Schattenschicht komplett ausgeblendet wird (Hoehle mit drei Karten-Hashes). */
int alundra_room_hide_clouds(void) {
    /* Rand-Signatur (erste/letzte Kartenspalte): bleibt stabil, wenn Gras/Steine den Karteninhalt aendern. */
    if (alundra_room_border_sig() == 0x28BF794Du) return 1;
    switch (alundra_room_sig()) {
    case 0x90C7456Eu:
    case 0x489CF39Au:
    case 0xC114DD8Eu:
        return 1;
    default:
        return 0;
    }
}

static void alundra_bgcolor_hook(struct CPUState* cpu, uint32_t address) {
    (void)address;
    uint32_t w = cpu->gpr[5];
    if ((w >> 24) != 0x02u) return;
    /* nur das Vollbild-Clear: Wortfolge 02BBGGRR, xy, 0x00F00140 (240 x 320); a3 zeigt bereits aufs xy-Wort */
    if (psx_mod_read_word(cpu->gpr[7] + 4u) != 0x00F00140u) return;
    g_alu_clear_rgb = w & 0x00FFFFFFu; /* tatsaechliche Clear-Farbe (fuer Pillarbox) */
    if (psx_mod_widescreen_view_x_margin() <= 0) return;
    uint32_t sig = alundra_room_sig();
    if (sig == 0) return; /* keine Karte geladen (Laden/Menue): Spiel-Clear lassen */
    cpu->gpr[5] = 0x02000000u | (alundra_room_color(sig) & 0x00FFFFFFu);
    g_alu_clear_rgb = cpu->gpr[5] & 0x00FFFFFFu;
}

static void alundra_bgcolor_activate(void) {}

PSX_MOD_CONSTRUCTOR(alundra_register_bgcolor_plugin) {
    (void)psx_mod_register_activation_plugin("alundra.bgcolor", alundra_bgcolor_activate);
    (void)psx_mod_register_instruction_plugin("alundra.bgcolor", ALU_BG_SITE, ALU_BG_WORD, alundra_bgcolor_hook);
}
