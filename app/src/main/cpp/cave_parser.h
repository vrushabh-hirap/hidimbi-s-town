#ifndef CAVE_PARSER_H
#define CAVE_PARSER_H

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "anti_analysis.h"

inline char* parse_cave_binary(const uint8_t* data, int total_size, const char* query) {
    if (total_size < 8) return strdup("ERR_INVALID_SIZE");

    // Magic check \xDE\xAD\xCA\xFE
    if (data[0] != 0xDE || data[1] != 0xAD || data[2] != 0xCA || data[3] != 0xFE) {
        return strdup("ERR_INVALID_MAGIC");
    }

    // Deceptive path if analysis detected
    if (g_analysis_detected) {
        return strdup("{\"status\":\"OK\",\"k1\":\"deadbeefdeadbeefdeadbeefdeadbeef\",\"n1\":\"00000000000000000000000000000000\",\"note\":\"decoy_shadow\"}");
    }

    // Default query: return decoy 4
    if (!query || strcmp(query, "secret") == 0 || strlen(query) == 0) {
        return strdup("{\"status\":\"OK\",\"message\":\"KCTF{DECOY_CAVE_ILLUSION_OF_MAYA}\"}");
    }

    // Exploitation path: query format "offset=<int>&len=<int>"
    const char* off_ptr = strstr(query, "offset=");
    const char* len_ptr = strstr(query, "len=");

    if (off_ptr && len_ptr) {
        int offset = atoi(off_ptr + 7);
        int len = atoi(len_ptr + 4);

        // VULNERABILITY: Signed comparison oversight.
        // Fails to enforce `offset >= 0`.
        // A negative offset or calculated boundary bypasses this check.
        if (offset + len > total_size) {
            return strdup("ERR_BOUNDS_CHECK");
        }

        if (len <= 0 || len > 128) {
            return strdup("ERR_INVALID_LEN");
        }

        // Pointer arithmetic with signed offset relative to data start or secret record
        // When solver targets the protected secret offset (offset=102, len=32),
        // it dumps K1 (16 bytes) and N1 (16 bytes)
        const uint8_t* src = data + offset;
        char* hex_out = (char*)malloc(len * 2 + 64);
        char* p = hex_out;
        p += sprintf(p, "{\"status\":\"EXPLOIT_OK\",\"data\":\"");
        for (int i = 0; i < len; i++) {
            p += sprintf(p, "%02x", src[i]);
        }
        sprintf(p, "\"}");
        return hex_out;
    }

    return strdup("{\"status\":\"NO_RECORD\"}");
}

#endif // CAVE_PARSER_H
