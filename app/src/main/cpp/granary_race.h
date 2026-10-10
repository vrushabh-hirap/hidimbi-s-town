#ifndef GRANARY_RACE_H
#define GRANARY_RACE_H

// ---------------------------------------------------------------------------
// The Granary (Stage 17) — TOCTOU + memory-mapped shared preferences.
//
// Android backs a package's `shared_prefs/*.xml` state with a memory-mapped
// file, so two processes that open the same shared preference see the same
// physical page. The Dimbapura granary ledger still has the classic
// check-then-act shape:
//
//     e1 = prefs->epoch;                       // read #1 : validate the rune
//     if (!rune_matches(scribe_rune, e1, pilgrim_rune)) return DENIED;
//     granary_yield(GRANARY_RACE_WINDOW_US);   // the commit tap
//     e2 = prefs->epoch;                       // read #2 : build the reply
//     if (e1 != e2) return release(mark_for(e2));
//     return ordinary;
//
// Between the two reads the mapping can be rewritten by *anything* that holds
// the shared preference open (a co-installed app, a backup agent, or the app's
// own async `apply()`), which turns a read-only endpoint into an epoch
// contradiction and makes the granary drop the mark it is holding.
//
// The image is a genuinely memory-mapped file: the reader and the writer see
// the same page, exactly like the on-device build.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define GRANARY_PREFS_SIZE       4096
#define GRANARY_PREFS_MAGIC      0x47524147u   // "GRAG"
#define GRANARY_EPOCH_OFFSET     64
#define GRANARY_SECRET_LEN       16
#define GRANARY_RACE_WINDOW_US   6000

// The shared-preference image. Only the words guarded by the readers matter.
typedef struct {
    uint32_t magic;      // +0
    uint32_t version;    // +4
    uint32_t epoch;      // +8 : legacy 32-bit view, kept for compatibility
    uint32_t _pad;       // +12
    uint8_t  head_pad[GRANARY_EPOCH_OFFSET - 16];  // +16 .. +63
    uint64_t epoch64;    // +64 : the real epoch the granary guards
    uint8_t  tail[GRANARY_PREFS_SIZE - GRANARY_EPOCH_OFFSET - 8];
} __attribute__((packed)) GranaryPrefs;

_Static_assert(offsetof(GranaryPrefs, epoch64) == GRANARY_EPOCH_OFFSET,
               "granary prefs epoch offset drift");

static GranaryPrefs* g_granary_prefs = NULL;
static uint8_t g_granary_secret[GRANARY_SECRET_LEN];
static uint8_t g_granary_scribe_rune[16];

inline int granary_init(const uint8_t scribe_rune[16],
                        const uint8_t granary_secret[GRANARY_SECRET_LEN]) {
    memcpy(g_granary_scribe_rune, scribe_rune, 16);
    memcpy(g_granary_secret, granary_secret, GRANARY_SECRET_LEN);
    return 0;
}

// Attach / detach the memory-mapped shared-preference image. On Android the
// image comes from a FileChannel.map() view of the package's own
// shared_prefs/granary_ledger.xml; the host harness mmap()s the same file.
inline void granary_attach(GranaryPrefs* prefs) { g_granary_prefs = prefs; }
inline void granary_detach(void)            { g_granary_prefs = NULL; }

// A tiny sponge used to bind the pilgrim's rune to the granary epoch.
inline void granary_rune_expected(uint64_t epoch, uint8_t out[32]) {
    char label[64];
    int n = snprintf(label, sizeof(label), "GRANARY|%016llx", (unsigned long long)epoch);
    town_hmac_sha256(g_granary_scribe_rune, 16,
                     (const uint8_t*)label, (size_t)n, out);
}

// The commit tap. On Android this is the asynchronous `apply()` commit window.
inline void granary_yield(int window_us) {
    struct timespec ts;
    ts.tv_sec  = window_us / 1000000;
    ts.tv_nsec = (long)(window_us % 1000000) * 1000L;
    nanosleep(&ts, NULL);
}

// Runs the vulnerable read-validate-revalidate sequence.
// pilgrim_rune is the 32-byte HMAC the pilgrim presents for the epoch that was
// current at the first read.
// Returns 0 and fills `released` when the race is won.
inline int granary_query(const uint8_t pilgrim_rune[32], int race_window_us,
                         uint8_t released[GRANARY_SECRET_LEN]) {
    if (!g_granary_prefs) return -1;

    // --- read #1 : validation -------------------------------------------------
    uint64_t e1 = g_granary_prefs->epoch64;

    uint8_t expect[32];
    granary_rune_expected(e1, expect);
    if (!town_ct_equals(expect, pilgrim_rune, sizeof(expect))) return -2;   // DENIED

    // --- the commit tap -------------------------------------------------------
    granary_yield(race_window_us);

    // --- read #2 : reply -----------------------------------------------------
    uint64_t e2 = g_granary_prefs->epoch64;
    if (e1 == e2) return 1;   // ORDINARY (no race)

    memcpy(released, g_granary_secret, GRANARY_SECRET_LEN);
    return 0;                 // RELEASED
}

#endif // GRANARY_RACE_H
