// ---------------------------------------------------------------------------
// tools/host_harness/harness.cpp
//
// A host build of the exact C++ that ships inside libtown.so, used by
// solve_scripts/solve_hidimbi.py to verify the client-side "under-city" stages
// on a machine with no Android device attached.
//
// Build:
//   clang++ -std=c++17 -O2 -o harness harness.cpp
//
// Protocol (one request per line on stdin, JSON reply on stdout):
//   GDSETUP  <epoch_u64_dec> <prefs_path>      -> {"ok":1}
//   GRANARY_SET <epoch_u64_dec>                -> {"ok":1}
//   VM       <program_b64> <seed_hex> <gate_word>
//                                                -> {"ok":1,"r":[r0,r1,r2,r3]}
//   DRUM     <note_b64> <mark_hex> <gate_tag_hex>
//                                                -> {"ok":1,"log":"..."}
//   COURIER  <scroll_b64> <gate_key_hex>       -> {"ok":1,"len":N,"echo_hex":"..."}
//   GRANARY  <rune_hex> <window_us> <delay_us> <new_epoch>
//                                                -> {"rc":0..,"secret_hex":".."}
//   QUIT
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

#include "../../app/src/main/cpp/town_crypto.h"
#include "../../app/src/main/cpp/scriptorium_vm.h"
#include "../../app/src/main/cpp/drum_tower.h"
#include "../../app/src/main/cpp/courier_ledger.h"
#include "../../app/src/main/cpp/granary_race.h"

#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

static void reply(const std::string& j) { fputs(j.c_str(), stdout); fputc('\n', stdout); fflush(stdout); }

static int b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static bool b64_decode(const std::string& in, std::string& out) {
    out.clear();
    int buf = 0, bits = 0;
    for (char c : in) {
        if (c == '=') continue;
        int v = b64_value(c);
        if (v < 0) return false;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return true;
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace((unsigned char)s[i])) ++i;
        size_t j = i;
        while (j < s.size() && !isspace((unsigned char)s[j])) ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

static std::string json_escape(const std::string& s) {
    std::string e;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { e.push_back('\\'); e.push_back((char)c); }
        else if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); e += b; }
        else e.push_back((char)c);
    }
    return e;
}

static std::string read_server_material(const char* env, const char* fallback) {
    const char* v = getenv(env);
    return v ? std::string(v) : std::string(fallback);
}

int main() {
    // ---- material owned by the tower / courier desk / granary --------------
    // Supplied by solve_hidimbi.py through the environment so the harness never
    // carries a secret it was not handed.
    uint8_t drum_key[32], scribe_rune[16], ctag[16], gsecret[16];
    memset(drum_key, 0, sizeof(drum_key));
    memset(scribe_rune, 0, sizeof(scribe_rune));
    memset(ctag, 0, sizeof(ctag));
    memset(gsecret, 0, sizeof(gsecret));
    { std::string h = read_server_material("DRUM_KEY", "00"); town_hex_decode(h.c_str(), drum_key, sizeof(drum_key)); }
    { std::string h = read_server_material("SCRIBE_RUNE", "00"); town_hex_decode(h.c_str(), scribe_rune, sizeof(scribe_rune)); }
    { std::string h = read_server_material("COURIER_CONFIG_TAG", "00"); town_hex_decode(h.c_str(), ctag, sizeof(ctag)); }
    { std::string h = read_server_material("GRANARY_SECRET", "00"); town_hex_decode(h.c_str(), gsecret, sizeof(gsecret)); }

    drum_tower_init(drum_key);
    courier_init(scribe_rune, ctag);
    granary_init(scribe_rune, gsecret);

    int prefs_fd = -1;
    std::string prefs_path;

    char line[65536];
    while (fgets(line, sizeof(line), stdin)) {
        std::string raw(line);
        while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r')) raw.pop_back();
        if (raw.empty()) continue;
        std::vector<std::string> t = split_ws(raw);
        const std::string& cmd = t[0];
        auto arg = [&](size_t i) -> std::string { return i + 1 < t.size() ? t[i + 1] : std::string(); };

        if (cmd == "QUIT") break;

        // -------------------------------------------------------------- GDSETUP
        if (cmd == "GDSETUP") {
            if (prefs_fd >= 0) { munmap(g_granary_prefs, GRANARY_PREFS_SIZE); close(prefs_fd); prefs_fd = -1; }
            prefs_path = arg(1);
            prefs_fd = open(prefs_path.c_str(), O_RDWR | O_CREAT, 0600);
            if (prefs_fd < 0) { reply("{\"ok\":0,\"err\":\"OPEN\"}"); continue; }
            if (ftruncate(prefs_fd, GRANARY_PREFS_SIZE) != 0) { reply("{\"ok\":0,\"err\":\"TRUNC\"}"); close(prefs_fd); prefs_fd = -1; continue; }
            GranaryPrefs* prefs = (GranaryPrefs*)mmap(NULL, GRANARY_PREFS_SIZE,
                                                     PROT_READ | PROT_WRITE, MAP_SHARED,
                                                     prefs_fd, 0);
            if (prefs == MAP_FAILED) { reply("{\"ok\":0,\"err\":\"MMAP\"}"); close(prefs_fd); prefs_fd = -1; continue; }
            memset(prefs, 0, GRANARY_PREFS_SIZE);
            prefs->magic   = GRANARY_PREFS_MAGIC;
            prefs->version = 1;
            prefs->epoch   = 1;
            prefs->epoch64 = strtoull(arg(0).c_str(), NULL, 10);
            g_granary_prefs = prefs;
            reply("{\"ok\":1}");
            continue;
        }

        // ---------------------------------------------------------- GRANARY_SET
        if (cmd == "GRANARY_SET") {
            if (!g_granary_prefs) { reply("{\"ok\":0,\"err\":\"NOT_MAPPED\"}"); continue; }
            g_granary_prefs->epoch64 = strtoull(arg(0).c_str(), NULL, 10);
            reply("{\"ok\":1}");
            continue;
        }

        // ------------------------------------------------------------------- VM
        if (cmd == "VM") {
            std::string prog;
            if (!b64_decode(arg(0), prog)) { reply("{\"ok\":0,\"err\":\"B64\"}"); continue; }
            uint8_t seed[8192];
            int sl = town_hex_decode(arg(1).c_str(), seed, sizeof(seed));
            if (sl < 0) { reply("{\"ok\":0,\"err\":\"SEED_HEX\"}"); continue; }
            uint32_t gate_word = (uint32_t)strtoul(arg(2).c_str(), NULL, 0);
            uint32_t out[4];
            char err[192];
            int rc = scriptorium_run((const uint8_t*)prog.data(), prog.size(),
                                     seed, (size_t)sl, gate_word, out, err, sizeof(err));
            if (rc == 0) {
                char buf[160];
                snprintf(buf, sizeof(buf), "{\"ok\":1,\"r\":[%u,%u,%u,%u]}",
                         out[0], out[1], out[2], out[3]);
                reply(buf);
            } else {
                reply(std::string("{\"ok\":0,\"err\":\"") + json_escape(err) + "\"}");
            }
            continue;
        }

        // ----------------------------------------------------------------- DRUM
        if (cmd == "DRUM") {
            std::string note;
            if (!b64_decode(arg(0), note)) { reply("{\"ok\":0,\"err\":\"B64\"}"); continue; }
            uint8_t mark[16];
            if (town_hex_decode(arg(1).c_str(), mark, sizeof(mark)) != 16) { reply("{\"ok\":0,\"err\":\"MARK\"}"); continue; }
            int gate_ok = 0;
            char* log = drum_tower_log(note.c_str(), mark, arg(2).c_str(), &gate_ok);
            if (!log) { reply("{\"ok\":0,\"err\":\"TOWER_GATE\"}"); continue; }
            reply(std::string("{\"ok\":1,\"log\":\"") + json_escape(log) + "\"}");
            free(log);
            continue;
        }

        // -------------------------------------------------------------- COURIER
        if (cmd == "COURIER") {
            std::string scroll;
            if (!b64_decode(arg(0), scroll)) { reply("{\"ok\":0,\"err\":\"B64\"}"); continue; }
            uint8_t gate_key[16];
            if (town_hex_decode(arg(1).c_str(), gate_key, sizeof(gate_key)) != 16) { reply("{\"ok\":0,\"err\":\"GATEKEY\"}"); continue; }
            char err[192];
            int rc = courier_parse_scroll((const uint8_t*)scroll.data(), scroll.size(),
                                          gate_key, err, sizeof(err));
            if (rc != 0) { reply(std::string("{\"ok\":0,\"err\":\"") + json_escape(err) + "\"}"); continue; }
            const uint8_t* echo;
            size_t n = courier_reply(&echo);
            if (n == 0) { reply("{\"ok\":0,\"err\":\"EMPTY_ECHO\"}"); continue; }
            char* hex = town_hex_encode(echo, n);
            char buf[64];
            snprintf(buf, sizeof(buf), "{\"ok\":1,\"len\":%zu,\"echo_hex\":\"", n);
            reply(std::string(buf) + hex + "\"}");
            free(hex);
            continue;
        }

        // --------------------------------------------------------------- GRANARY
        if (cmd == "GRANARY") {
            if (!g_granary_prefs) { reply("{\"ok\":0,\"err\":\"NOT_MAPPED\"}"); continue; }
            uint8_t rune[32];
            if (town_hex_decode(arg(0).c_str(), rune, sizeof(rune)) != 32) { reply("{\"ok\":0,\"err\":\"RUNE\"}"); continue; }
            int window_us = atoi(arg(1).c_str());
            int delay_us  = atoi(arg(2).c_str());
            unsigned long long new_epoch = strtoull(arg(3).c_str(), NULL, 10);

            // The racing writer: a second holder of the shared preference that
            // rewrites the very same page during the commit window.
            auto writer = [&]() {
                std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
                g_granary_prefs->epoch64 = (uint64_t)new_epoch;
            };
            std::thread t(writer);
            uint8_t secret[16];
            int rc = granary_query(rune, window_us, secret);
            t.join();
            if (rc == 0) {
                char* hex = town_hex_encode(secret, 16);
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"rc\":0,\"secret_hex\":\"");
                reply(std::string(buf) + hex + "\"}");
                free(hex);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"rc\":%d}", rc);
                reply(buf);
            }
            continue;
        }

        reply("{\"ok\":0,\"err\":\"UNKNOWN_CMD\"}");
    }

    if (prefs_fd >= 0) {
        munmap(g_granary_prefs, GRANARY_PREFS_SIZE);
        close(prefs_fd);
        if (!prefs_path.empty()) unlink(prefs_path.c_str());
    }
    return 0;
}
