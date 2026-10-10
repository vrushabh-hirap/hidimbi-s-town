#ifndef COURIER_LEDGER_H
#define COURIER_LEDGER_H

// ---------------------------------------------------------------------------
// The Courier's Ledger (Stage 16) — protobuf wire-format parsing abuse.
//
// The Dimbapura courier desk reads pilgrim scrolls with a hand-rolled
// protobuf reader (no descriptor pool, no generated stubs). The schema it
// implements is:
//
//     message CourierScroll {
//         optional uint32 seal    = 1;   // varint  (wire type 0)
//         optional bytes  note    = 2;   // length-delimited (wire type 2)
//         optional Fragment bag   = 3;   // nested message (wire type 2)
//                                       // ...or, for legacy scrolls, a
//                                       // Group (wire type 3) terminated by
//                                       // field 3 with wire type 4.
//     }
//
// The vulnerability is a wire-type confusion on field 3. A nested message is
// normally length-delimited, so the reader knows exactly where the fragment
// ends. For backwards compatibility with the old sign-and-seal format the
// reader *also* accepts a Group in its place. The group path is:
//
//     const uint8_t* frag = cursor;
//     size_t c = skip_group(cursor, end);       // scans for the EndGroup tag
//     size_t frag_len = c - frag;
//     g_bag.echo_len = frag_len;                // <-- UNCLAMPED
//     memcpy(g_bag.echo, frag, frag_len < 64 ? frag_len : 64);
//
// `echo_len` is written straight into the ledger bag that also holds the
// scribe rune, and the reply is `hex(g_bag.echo[0 : g_bag.echo_len])`. Because
// the bounds clamp and the reported length are computed on different code
// paths, a scroll whose unclosed group runs past the end of the supplied
// buffer makes `echo_len` exceed 64 and the reply reads past `echo` into the
// adjacent scribe rune. That is the whole exploit: one StartGroup tag and a
// payload long enough to push `echo_len` past 64.
//
// The desk first demands `seal`, which is bound to the Stage-15 drum key, so a
// pilgrim cannot bypass the tower above.
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#define COURIER_SEAL_WORDS 4
#define COURIER_ECHO_BYTES 64

// The ledger bag in .bss. Field order is fixed and part of the attack.
typedef struct {
    uint8_t  echo[COURIER_ECHO_BYTES];   // +0   64 bytes
    uint32_t echo_len;                   // +64  attacker influenced, unclamped
    uint8_t  scribe_rune[16];            // +68  the secret we want
    uint8_t  config_tag[16];             // +84  tower configuration tag
    uint32_t checksum;                   // +100
} CourierBag;

static CourierBag g_courier_bag;

inline void courier_init(const uint8_t scribe_rune[16], const uint8_t config_tag[16]) {
    memset(&g_courier_bag, 0, sizeof(g_courier_bag));
    memcpy(g_courier_bag.scribe_rune, scribe_rune, 16);
    memcpy(g_courier_bag.config_tag, config_tag, 16);
    {   // recompute the bag checksum from the secret material
        uint8_t dig[32];
        town_hmac_sha256(g_courier_bag.scribe_rune, 16, (const uint8_t*)"COURIER", 7, dig);
        g_courier_bag.checksum = (uint32_t)dig[0] | ((uint32_t)dig[1] << 8) |
                                 ((uint32_t)dig[2] << 16) | ((uint32_t)dig[3] << 24);
    }
}

// ---- varint / tag helpers -------------------------------------------------

inline int courier_read_varint(const uint8_t* buf, size_t len, size_t* cursor,
                               uint64_t* value) {
    uint64_t result = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
        if (*cursor >= len) return -1;
        uint8_t b = buf[(*cursor)++];
        result |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *value = result; return 0; }
        shift += 7;
    }
    return -1;
}

inline int courier_skip_unknown(uint32_t wire, const uint8_t* buf, size_t len,
                                size_t* cursor) {
    switch (wire) {
        case 0: { // varint
            uint64_t v;
            return courier_read_varint(buf, len, cursor, &v);
        }
        case 1: // 64-bit
            if (*cursor + 8 > len) return -1;
            *cursor += 8;
            return 0;
        case 5: // 32-bit
            if (*cursor + 4 > len) return -1;
            *cursor += 4;
            return 0;
        case 2: { // length-delimited
            uint64_t l;
            if (courier_read_varint(buf, len, cursor, &l) != 0) return -1;
            if (l > (uint64_t)(len - *cursor)) return -1;
            *cursor += (size_t)l;
            return 0;
        }
        default:
            return -1; // wire types 3 and 4 are handled by the group scanner
    }
}

// Scans forward for the EndGroup that matches the StartGroup the caller just
// consumed. Returns 0 on success and leaves *out at the first byte after the
// group; returns -1 (with *out at the buffer end) when the group is never
// closed — which is exactly the condition the exploit relies on.
inline int courier_skip_group(const uint8_t* buf, size_t len, size_t* cursor,
                              size_t* out) {
    int nesting = 0;
    while (*cursor < len) {
        uint64_t tag;
        if (courier_read_varint(buf, len, cursor, &tag) != 0) { *out = *cursor; return -1; }
        uint32_t wire = (uint32_t)(tag & 7);
        if (wire == 3) { ++nesting; continue; }
        if (wire == 4) {
            if (nesting == 0) { *out = *cursor; return 0; }
            --nesting;
            continue;
        }
        if (courier_skip_unknown(wire, buf, len, cursor) != 0) { *out = *cursor; return -1; }
    }
    *out = *cursor;
    return -1;
}

// Parses one courier scroll. gate_key is the Stage-15 drum key (first 16
// bytes). Returns 0 on success; on failure err holds a NUL-terminated reason.
inline int courier_parse_scroll(const uint8_t* buf, size_t len,
                                const uint8_t gate_key[16],
                                char* err, size_t err_cap) {
    if (len < 2) { snprintf(err, err_cap, "ERR_SHORT_SCROLL"); return -1; }

    memset(&g_courier_bag.echo, 0, sizeof(g_courier_bag.echo));
    g_courier_bag.echo_len = 0;

    size_t cursor = 0;
    int    saw_seal = 0;
    int    closed_ok = 0;

    while (cursor < len) {
        uint64_t tag;
        if (courier_read_varint(buf, len, &cursor, &tag) != 0) {
            snprintf(err, err_cap, "ERR_TAG_VARINT");
            return -1;
        }
        uint32_t field = (uint32_t)(tag >> 3);
        uint32_t wire  = (uint32_t)(tag & 7);

        if (field == 1 && wire == 0) {
            uint64_t seal;
            if (courier_read_varint(buf, len, &cursor, &seal) != 0) {
                snprintf(err, err_cap, "ERR_SEAL_VARINT");
                return -1;
            }
            // The desk seal is the first four bytes of SHA-256(gate_key).
            uint8_t dig[32];
            town_sha256(gate_key, 16, dig);
            uint32_t expect = (uint32_t)dig[0] | ((uint32_t)dig[1] << 8) |
                              ((uint32_t)dig[2] << 16) | ((uint32_t)dig[3] << 24);
            if ((uint32_t)seal != expect) {
                snprintf(err, err_cap, "ERR_BAD_SEAL");
                return -1;
            }
            saw_seal = 1;
            continue;
        }

        if (field == 3 && (wire == 2 || wire == 3)) {
            const uint8_t* frag = buf + cursor;
            size_t frag_len = 0;

            if (wire == 2) {
                uint64_t declared;
                if (courier_read_varint(buf, len, &cursor, &declared) != 0) {
                    snprintf(err, err_cap, "ERR_FRAG_LEN");
                    return -1;
                }
                if (declared > (uint64_t)(len - cursor)) {
                    snprintf(err, err_cap, "ERR_FRAG_OOB");
                    return -1;
                }
                frag = buf + cursor;
                frag_len = (size_t)declared;
                cursor += frag_len;
            } else {
                // --- the vulnerable path -----------------------------------
                // Group encoding: the reader recurses into the *enclosing*
                // buffer and takes the group's total content length as the
                // fragment length, without clamping it to the echo buffer.
                size_t group_end;
                int closed = courier_skip_group(buf, len, &cursor, &group_end);
                frag_len = group_end - (size_t)(frag - buf);
                closed_ok = (closed == 0) ? 1 : closed_ok;
            }

            size_t copy = frag_len < COURIER_ECHO_BYTES ? frag_len : COURIER_ECHO_BYTES;
            memcpy(g_courier_bag.echo, frag, copy);
            g_courier_bag.echo_len = (uint32_t)frag_len;   // <-- unclamped
            continue;
        }

        if (courier_skip_unknown(wire, buf, len, &cursor) != 0) {
            snprintf(err, err_cap, "ERR_UNKNOWN_FIELD_%u", field);
            return -1;
        }
    }

    if (!saw_seal) { snprintf(err, err_cap, "ERR_MISSING_SEAL"); return -1; }

    (void)closed_ok;
    snprintf(err, err_cap, "OK");
    return 0;
}

// Produces the reply the pilgrim sees: the ledger "echo", emitted with the
// bag's own — attacker influenced — length.
inline size_t courier_reply(const uint8_t** data) {
    const uint8_t* base = g_courier_bag.echo;
    size_t reported = g_courier_bag.echo_len;
    *data = base;
    return reported;
}

#endif // COURIER_LEDGER_H
