/*
 * Alundra mods: gemeinsame Konstanten und Hilfsfunktionen (nur Header).
 * Alle Funktionen sind static inline, jede Mod-Datei darf das Header einbinden.
 */
#ifndef ALUNDRA_COMMON_H
#define ALUNDRA_COMMON_H

#include <stdint.h>
#include "mod_plugins.h"

#define ALU_TILE_W        24
#define ALU_MAP_COLS      52  /* Außenkarten; Innenräume können abweichen */
#define ALU_MAP_PX        (ALU_MAP_COLS * ALU_TILE_W)
#define ALU_CAM_X         0x800E4328u
#define ALU_CAM_Y         0x800E432Cu
#define ALU_MAP_ROWS_PTR  0x800DD86Cu /* Zeilenbasis, 60 Zeilen à 416 B */

/* Raum-Signatur: FNV-1a über die Kartenzeilen (0 = keine Karte geladen). */
static inline uint32_t alundra_room_sig(void) {
    uint32_t p = psx_mod_read_word(ALU_MAP_ROWS_PTR);
    if ((p & 0xFF000000u) != 0x80000000u) return 0;
    uint32_t h = 0x811C9DC5u;
    for (uint32_t i = 0; i < 416u * 60u; i += 4u) h = (h ^ psx_mod_read_word(p + i)) * 0x01000193u;
    return h;
}

/* Signatur nur über erste und letzte Kartenspalte (stabiler als die volle). */
static inline uint32_t alundra_room_border_sig(void) {
    uint32_t p = psx_mod_read_word(ALU_MAP_ROWS_PTR);
    if ((p & 0xFF000000u) != 0x80000000u) return 0;
    uint32_t h = 0x811C9DC5u;
    for (uint32_t r = 0; r < 60u; r++)
        for (uint32_t o = 0; o < 416u; o += 408u)
            for (uint32_t k = 0; k < 8u; k += 4u)
                h = (h ^ psx_mod_read_word(p + r * 416u + o + k)) * 0x01000193u;
    return h;
}

#endif
