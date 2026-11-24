/* Alundra: Test-Cheats (Feature cheat_infinite_hp, Plugin alundra.cheat_hp).
 *
 * Unendlich HP: jeden VBlank wird die aktuelle HP auf das Maximum gesetzt.
 * Die HP liegen mehrfach im RAM (Kopien/Anzeigewerte), alle werden gehalten:
 *   0x800A8284 (max bei +4), 0x80127D44 (max bei +4), 0x801EB82C (max bei +2).
 * Nur wenn aktuelle HP > 0 und Maximum plausibel ist, damit Tod/Titel/Menues
 * nicht gestoert werden. Standardmaessig AUS (default_enabled = false).
 * Im Spiel umschaltbar: L1 + R1 + Quadrat (Pad Spieler 1, Flanke). */
#include "mod_plugins.h"

typedef struct { uint32_t cur; uint32_t max_off; } AluHpSlot;
static const AluHpSlot k_slots[] = {
    /* US (SLUS-00553), per RAM-Diff 10 -> 6 gefunden. DE: 0x800A8284, 0x80127D44, 0x801EB82C(+2). */
    { 0x800AA0ECu, 4u },
    { 0x801AC70Cu, 4u },
};

/* PSX-Buttons, active-high: Quadrat=bit15, L1=bit10, R1=bit11. */
#define ALU_CHEAT_COMBO ((1u << 15) | (1u << 10) | (1u << 11))

static int g_hp_cheat_on = 1;
static PSXModDigitalPadReader g_pad = { sizeof(PSXModDigitalPadReader) };
static int g_combo_held = 0;

static void alundra_cheat_poll_toggle(void) {
    uint32_t buttons = 0;
    if (!psx_mod_read_digital_pad(0, &g_pad, 0, &buttons)) return;
    int held = (buttons & ALU_CHEAT_COMBO) == ALU_CHEAT_COMBO;
    if (held && !g_combo_held) g_hp_cheat_on = !g_hp_cheat_on;
    g_combo_held = held;
}

static void alundra_cheat_hp_vblank(void) {
    if (!psx_mod_game_started()) return;
    alundra_cheat_poll_toggle();
    if (!g_hp_cheat_on) return;
    for (unsigned i = 0; i < sizeof(k_slots) / sizeof(k_slots[0]); ++i) {
        uint16_t cur = psx_mod_read_half(k_slots[i].cur);
        uint16_t max = psx_mod_read_half(k_slots[i].cur + k_slots[i].max_off);
        if (cur > 0u && max >= 1u && max <= 200u && cur < max)
            psx_mod_write_half(k_slots[i].cur, max);
    }
}

PSX_MOD_CONSTRUCTOR(alundra_register_cheat_plugin) {
    (void)psx_mod_register_vblank_plugin("alundra.cheat_hp", alundra_cheat_hp_vblank);
}
