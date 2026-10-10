#ifndef DRUM_TOWER_H
#define DRUM_TOWER_H

// ---------------------------------------------------------------------------
// The Drum Tower (Stage 15) — a classic format-string vulnerability.
//
// The Dimbapura drum tower still logs pilgrim notes through a legacy C
// routine that was never converted to a fixed format specifier:
//
//     static const char* log_drum(const char* note) {
//         // ... legacy: the caller-supplied buffer IS the format string.
//         return drum_sprintf(note, tower_resonance_frame());
//     }
//
// `note` is the raw scroll string the pilgrim supplies, so any positional
// parameter (`%n$p`, `%n$x`, `%n$s`) reaches the tower's implicit argument
// window. The window is a frame of DRUM_WINDOW_SLOTS 32-bit "resonance words"
// that live beside the tower's drum key in .bss.
//
// Window layout (1-based, exactly as `%n$` addresses it):
//     slot 1        : DRUM_MARKER_WORD (a recognisable sentinel)
//     slot 2 .. 9   : the 32-byte tower drum key (little-endian words)
//     slot 10 .. 12 : padding words
//
// Before the log line is emitted the tower demands a proof keyed to the
// Stage-14 scriptorium mark, so the tower will not speak to a pilgrim who has
// not already assembled the mosaic seed and run a bytecode program against it.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define DRUM_WINDOW_SLOTS 12
#define DRUM_KEY_LEN      32
#define DRUM_MARKER_WORD  0xD1B1D1B1u

// Hardcoded inside libtown.so, exactly like the Stage-11 KDF constants.
#define TOWN_K1_HEX "4a8f9c2d1e0b5a3f7c8d9e2a1b0c3d4e"
#define TOWN_K2_HEX "3b7e1a9c4f2d0e8b6a5c3e1f9d2a0b4c"

// The tower's .bss frame. `key` sits immediately after `window`, which is what
// makes the over-wide positional reads interesting.
typedef struct {
    uint32_t window[DRUM_WINDOW_SLOTS];
    uint8_t  key[DRUM_KEY_LEN];
    uint32_t checksum;
} DrumFrame;

static DrumFrame g_drum_frame;

inline void drum_tower_init(const uint8_t drum_key[DRUM_KEY_LEN]) {
    memset(&g_drum_frame, 0, sizeof(g_drum_frame));
    g_drum_frame.window[0]  = DRUM_MARKER_WORD;
    g_drum_frame.window[9]  = 0xC0FFEE11u;
    g_drum_frame.window[10] = 0xC0FFEE22u;
    g_drum_frame.window[11] = 0xDEADBEEFu;
    for (size_t w = 0; w < DRUM_KEY_LEN / 4; ++w)
        g_drum_frame.window[1 + w] = (uint32_t)drum_key[w * 4] |
                                     ((uint32_t)drum_key[w * 4 + 1] << 8) |
                                     ((uint32_t)drum_key[w * 4 + 2] << 16) |
                                     ((uint32_t)drum_key[w * 4 + 3] << 24);
    memcpy(g_drum_frame.key, drum_key, DRUM_KEY_LEN);
    uint8_t dig[32];
    town_hmac_sha256(g_drum_frame.key, DRUM_KEY_LEN, (const uint8_t*)"TOWER", 5, dig);
    g_drum_frame.checksum = (uint32_t)dig[0] | ((uint32_t)dig[1] << 8) |
                            ((uint32_t)dig[2] << 16) | ((uint32_t)dig[3] << 24);
}

// gate_mark is the 16-byte scriptorium_mark from Stage 14; gate_tag_hex is the
// pilgrim's claim. Returns 1 when the claim matches HMAC(K1||K2, mark).
inline int drum_tower_gate(const uint8_t gate_mark[16], const char* gate_tag_hex) {
    uint8_t k1[16], k2[16];
    if (town_hex_decode(TOWN_K1_HEX, k1, sizeof(k1)) != 16) return 0;
    if (town_hex_decode(TOWN_K2_HEX, k2, sizeof(k2)) != 16) return 0;
    uint8_t ikm[32];
    memcpy(ikm, k1, 16);
    memcpy(ikm + 16, k2, 16);

    uint8_t expect[32];
    town_hmac_sha256(ikm, sizeof(ikm), gate_mark, 16, expect);

    uint8_t presented[32];
    if (town_hex_decode(gate_tag_hex, presented, sizeof(presented)) != 32) return 0;
    return town_ct_equals(expect, presented, sizeof(expect));
}

// The tower's inscription ring: %n$s never dereferences raw words as pointers
// (that would segfault the host); instead it indexes this ring, exactly the
// way the on-device build's guarded `drum_strtable()` helper does.
static const char* g_drum_inscriptions[8] = {
    "the-rain-fell-on-the-granary",
    "hidimbi-counts-the-drums",
    "no-road-home-is-the-shortest",
    "the-well-remembers-nine-names",
    "ash-and-salt-and-bell-metal",
    "the-map-lies-loudly",
    "the-cave-forgets-nothing",
    "under-the-town-the-lake-sleeps"
};

// A deliberately faithful-but-tiny implementation of the vulnerable
// drum_sprintf(): it walks the caller-supplied format string and resolves
// `%[flags][width][.precision][length]positional$specifier`, reading the
// resolved slot out of the resonance window.
//
// Supported specifiers: d, u, x, X, p, s, c, %  — plus the write primitive
// `%n` / `%hn` / `%hhn`, which reports the emitted character count (exactly
// like %n) but performs no memory write, so a hostile scroll cannot corrupt
// the APK's own heap from the outside.
//
// Returns a malloc'd NUL-terminated string, or NULL when the gate fails.
inline char* drum_tower_log(const char* note, const uint8_t gate_mark[16],
                            const char* gate_tag_hex, int* gate_ok) {
    if (!drum_tower_gate(gate_mark, gate_tag_hex)) {
        if (gate_ok) *gate_ok = 0;
        return NULL;
    }
    if (gate_ok) *gate_ok = 1;

    const uint32_t* frame = g_drum_frame.window;
    int auto_slot = 1;   // non-positional parameters step through the window

    size_t out_cap = 512;
    char* out = (char*)malloc(out_cap);
    if (!out) return NULL;
    size_t used = 0;
    int failed = 0;

#define DRUM_EMIT(s) do { \
        const char* _s = (s); size_t _n = strlen(_s); \
        if (used + _n + 1 > out_cap) { out_cap = (used + _n + 65) * 2; \
            char* _r = (char*)realloc(out, out_cap); if (!_r) { free(out); failed = 1; goto done; } out = _r; } \
        memcpy(out + used, _s, _n); used += _n; \
    } while (0)

    size_t i = 0;
    while (note[i]) {
        if (note[i] != '%') {
            char lit[2] = { note[i], 0 };
            DRUM_EMIT(lit);
            ++i;
            continue;
        }

        size_t j = i + 1;
        if (note[j] == '%') { DRUM_EMIT("%"); i = j + 1; continue; }
        if (note[j] == 0) break;

        long positional = -1;
        long width = 0;
        int  length_hh = 0, length_h = 0;

        // optional positional index: digits followed by '$'
        {
            const char* digits = note + j;
            const char* scan = digits;
            while (*scan >= '0' && *scan <= '9') ++scan;
            if (scan > digits && *scan == '$') {
                positional = strtol(digits, NULL, 10);
                j = (size_t)(scan - note) + 1;
            }
        }
        while (note[j] == '-' || note[j] == '+' || note[j] == ' ' ||
               note[j] == '#' || note[j] == '0') ++j;
        if (note[j] >= '0' && note[j] <= '9') {
            width = strtol(note + j, NULL, 10);
            while (note[j] >= '0' && note[j] <= '9') ++j;
        }
        if (note[j] == '.') { ++j; while (note[j] >= '0' && note[j] <= '9') ++j; }
        if (note[j] == 'h' && note[j + 1] == 'h') { length_hh = 1; j += 2; }
        else if (note[j] == 'h') { length_h = 1; ++j; }
        else if (note[j] == 'l' || note[j] == 'z' || note[j] == 'j' ||
                 note[j] == 't') { ++j; }

        char spec = note[j] ? note[j] : '\0';
        char field[256];
        field[0] = 0;

        long slot_index;
        if (positional > 0) slot_index = positional - 1;
        else                slot_index = auto_slot++;

        uint32_t word = (slot_index >= 0 && slot_index < (long)DRUM_WINDOW_SLOTS)
                            ? frame[slot_index] : 0u;

        switch (spec) {
            case 'd':
            case 'u':
                snprintf(field, sizeof(field), "%lu", (unsigned long)word);
                break;
            case 'x':
                snprintf(field, sizeof(field), "%lx", (unsigned long)word);
                break;
            case 'X':
                snprintf(field, sizeof(field), "%lX", (unsigned long)word);
                break;
            case 'p':
                snprintf(field, sizeof(field), "0x%lx", (unsigned long)word);
                break;
            case 'c':
                snprintf(field, sizeof(field), "%c", (int)(word & 0xFF));
                break;
            case 's':
                snprintf(field, sizeof(field), "%s",
                         g_drum_inscriptions[(word >> 3) & 0x7]);
                break;
            case 'n':
                if (length_hh)      snprintf(field, sizeof(field), "%d", (int)(uint8_t)used);
                else if (length_h)  snprintf(field, sizeof(field), "%d", (int)(uint16_t)used);
                else                snprintf(field, sizeof(field), "%d", (int)used);
                break;
            default:
                snprintf(field, sizeof(field), "%%%c", spec ? spec : ' ');
                break;
        }

        if (field[0]) DRUM_EMIT(field);
        (void)width;

        i = (spec == '\0') ? j : j + 1;
    }

#undef DRUM_EMIT

done:
    if (failed || !out) return NULL;
    out[used] = 0;
    return out;
}

#endif // DRUM_TOWER_H
