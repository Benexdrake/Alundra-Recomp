/*
 * Alundra: Kamera im 16:9 auf die Karte beschraenken (Feature camera_clamp).
 * Das Spiel klemmt Kamera X bei 0x8002D188..0x8002D1B0 auf [0, 927] (4:3-Fenster).
 * In 16:9 sieht man dann je Seite m px (39) ueber den Kartenrand. Der Hook direkt nach
 * der Klemme (0x8002D1B8, Wort 0x3C04801F) verengt auf [m, rechts - m]. In allen Raeumen (Spaltenzahl
 * aus der Randquad-Tabelle, sonst 52). Der weiche Kartenrand wird dort
 * uebersprungen (g_alu_cam_clamp_on, pro Frame gesetzt).
 */
#include "alundra_common.h"
#include "cpu_state.h"

#define ALU_CAM_SITE 0x8002D1B8u
#define ALU_CAM_WORD 0x3C04801Fu

extern int32_t alundra_edge_room_cols(void); /* alundra_edgefade.c, 0 = unbekannter Raum */
int g_alu_cam_clamp_on;

static void alundra_camclamp_hook(struct CPUState* cpu, uint32_t address) {
    (void)cpu; (void)address;
    int32_t m = psx_mod_widescreen_view_x_margin();
    if (m <= 0) return;
    int32_t cols = alundra_edge_room_cols();
    if (cols <= 0) cols = ALU_MAP_COLS; /* Testmodus: unbekannte Raeume wie 52 Spalten (Klemme des Spiels ist fest 927) */
    int32_t lo = m, hi = cols * ALU_TILE_W - 321 - m; /* 927 - m bei 52 Spalten */
    if (hi < lo) return;
    int32_t cam = (int32_t)psx_mod_read_word(ALU_CAM_X);
    if (cam < lo) cam = lo;
    if (cam > hi) cam = hi;
    psx_mod_write_word(ALU_CAM_X, (uint32_t)cam);
    g_alu_cam_clamp_on = 1;
}

PSX_MOD_CONSTRUCTOR(alundra_register_camclamp_plugin) {
    (void)psx_mod_register_instruction_plugin("alundra.camclamp", ALU_CAM_SITE, ALU_CAM_WORD, alundra_camclamp_hook);
}
