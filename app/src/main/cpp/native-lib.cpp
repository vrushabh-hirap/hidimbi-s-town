#include <jni.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include "anti_analysis.h"
#include "cave_parser.h"
#include "town_crypto.h"
#include "scriptorium_vm.h"
#include "drum_tower.h"
#include "courier_ledger.h"
#include "granary_race.h"

// HKDF-SHA256 Extract and Expand (Stage 11 key derivation primitive)
static void hkdf_sha256(const uint8_t* salt, size_t salt_len,
                        const uint8_t* ikm, size_t ikm_len,
                        const uint8_t* info, size_t info_len,
                        uint8_t* okm, size_t okm_len) {
    uint8_t prk[32];
    town_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);

    uint8_t t[32] = {0};
    size_t t_len = 0;
    size_t generated = 0;
    uint8_t counter = 1;

    while (generated < okm_len) {
        TOWN_SHA256_CTX ctx;
        uint8_t step_in[128];
        size_t step_len = 0;
        if (t_len > 0) {
            memcpy(step_in, t, t_len);
            step_len += t_len;
        }
        memcpy(step_in + step_len, info, info_len);
        step_len += info_len;
        step_in[step_len++] = counter;

        town_hmac_sha256(prk, 32, step_in, step_len, t);
        t_len = 32;

        size_t to_copy = (okm_len - generated < 32) ? (okm_len - generated) : 32;
        memcpy(okm + generated, t, to_copy);
        generated += to_copy;
        counter++;
    }
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_initNative(JNIEnv *env, jobject thiz) {
    init_anti_analysis();
}

JNIEXPORT jstring JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_queryCaveStore(
        JNIEnv *env, jobject thiz, jobject asset_manager, jstring query_str) {

    AAssetManager* mgr = AAssetManager_fromJava(env, asset_manager);
    if (!mgr) return env->NewStringUTF("ERR_ASSET_MGR");

    AAsset* asset = AAssetManager_open(mgr, "cave_store.bin", AASSET_MODE_BUFFER);
    if (!asset) return env->NewStringUTF("ERR_OPEN_ASSET");

    size_t size = AAsset_getLength(asset);
    const uint8_t* buffer = (const uint8_t*)AAsset_getBuffer(asset);

    const char* q = query_str ? env->GetStringUTFChars(query_str, NULL) : "";
    char* result = parse_cave_binary(buffer, (int)size, q);

    if (query_str) env->ReleaseStringUTFChars(query_str, q);
    AAsset_close(asset);

    jstring ret = env->NewStringUTF(result);
    free(result);
    return ret;
}

JNIEXPORT jboolean JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_verifyForestToken(
        JNIEnv *env, jobject thiz, jbyteArray cert_bytes, jstring token_str) {

    if (g_analysis_detected) return JNI_FALSE;

    jsize cert_len = env->GetArrayLength(cert_bytes);
    jbyte* cert_data = env->GetByteArrayElements(cert_bytes, NULL);

    const char* salt = "FLAG{HIDIMBI_IS_WATCHING}";
    uint8_t out_hash[32];
    town_hmac_sha256((const uint8_t*)salt, strlen(salt), (const uint8_t*)cert_data, cert_len, out_hash);

    env->ReleaseByteArrayElements(cert_bytes, cert_data, JNI_ABORT);

    char expected_hex[65];
    for (int i = 0; i < 32; i++) {
        sprintf(expected_hex + (i * 2), "%02x", out_hash[i]);
    }
    expected_hex[64] = 0;

    const char* actual_token = env->GetStringUTFChars(token_str, NULL);
    bool match = (strcasecmp(expected_hex, actual_token) == 0);
    env->ReleaseStringUTFChars(token_str, actual_token);

    return match ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jbyteArray JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_deriveFinalKey(
        JNIEnv *env, jobject thiz,
        jbyteArray n1_arr, jbyteArray n2_arr, jbyteArray n3_arr, jbyteArray n4_arr) {

    // Hardcoded internal key components recovered from chain
    const char* k1_hex = "4a8f9c2d1e0b5a3f7c8d9e2a1b0c3d4e";
    const char* k2_hex = "3b7e1a9c4f2d0e8b6a5c3e1f9d2a0b4c";
    const char* k3_hex = "c9d8e7f6a5b4c3d2e1f0123456789abc";
    const char* k4_hex = "5f4e3d2c1b0a9f8e7d6c5b4a3f2e1d0c";

    auto parse_hex = [](const char* s, uint8_t* out) {
        for (int i = 0; i < 16; i++) {
            sscanf(s + 2*i, "%02hhx", &out[i]);
        }
    };

    uint8_t k1[16], k2[16], k3[16], k4[16];
    parse_hex(k1_hex, k1);
    parse_hex(k2_hex, k2);
    parse_hex(k3_hex, k3);
    parse_hex(k4_hex, k4);

    uint8_t salt[32];
    memcpy(salt, k1, 16);
    memcpy(salt + 16, k2, 16);

    uint8_t ikm[32];
    memcpy(ikm, k3, 16);
    memcpy(ikm + 16, k4, 16);

    jbyte* n1_b = env->GetByteArrayElements(n1_arr, NULL);
    jbyte* n2_b = env->GetByteArrayElements(n2_arr, NULL);
    jbyte* n3_b = env->GetByteArrayElements(n3_arr, NULL);
    jbyte* n4_b = env->GetByteArrayElements(n4_arr, NULL);

    uint8_t info[64];
    memcpy(info, n1_b, 16);
    memcpy(info + 16, n2_b, 16);
    memcpy(info + 32, n3_b, 16);
    memcpy(info + 48, n4_b, 16);

    env->ReleaseByteArrayElements(n1_arr, n1_b, JNI_ABORT);
    env->ReleaseByteArrayElements(n2_arr, n2_b, JNI_ABORT);
    env->ReleaseByteArrayElements(n3_arr, n3_b, JNI_ABORT);
    env->ReleaseByteArrayElements(n4_arr, n4_b, JNI_ABORT);

    uint8_t derived[44];
    hkdf_sha256(salt, 32, ikm, 32, info, 64, derived, 44);

    // If analysis detected, poison the key to yield decoy/fail
    if (g_analysis_detected) {
        memset(derived, 0xAA, 44);
    }

    jbyteArray result = env->NewByteArray(32);
    env->SetByteArrayRegion(result, 0, 32, (jbyte*)derived);
    return result;
}

// ===========================================================================
// Under-city stage exports (Stages 13-17).
//
// These are the native halves of the Scriptorium, the Drum Tower, the
// Courier's Ledger and the Granary. All of them are gated on material that
// can only be recovered by completing the earlier stages of the chain.
// ===========================================================================

// ---- Stage 14: The Scriptorium VM ----------------------------------------
JNIEXPORT jstring JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_runScriptorium(
        JNIEnv *env, jobject thiz,
        jbyteArray program, jbyteArray seed, jint gate_word) {

    if (g_analysis_detected) return env->NewStringUTF("ERR_SHADOW_EXECUTION");

    jsize prog_len = env->GetArrayLength(program);
    jsize seed_len = env->GetArrayLength(seed);

    std::vector<uint8_t> prog((size_t)prog_len);
    std::vector<uint8_t> seed_buf((size_t)seed_len);

    if (prog_len > 0) {
        env->GetByteArrayRegion(program, 0, prog_len, (jbyte*)prog.data());
    }
    if (seed_len > 0) {
        env->GetByteArrayRegion(seed, 0, seed_len, (jbyte*)seed_buf.data());
    }

    uint32_t out[4];
    char err[192] = {0};
    int rc = scriptorium_run(prog.data(), (size_t)prog_len,
                             seed_buf.data(), (size_t)seed_len,
                             (uint32_t)gate_word, out, err, sizeof(err));
    if (rc != 0) return env->NewStringUTF(err);

    // Pack R0..R3 little-endian: the 16-byte scriptorium mark.
    uint8_t mark[16];
    for (int i = 0; i < 4; ++i) {
        mark[i * 4 + 0] = (uint8_t)(out[i] & 0xFF);
        mark[i * 4 + 1] = (uint8_t)((out[i] >> 8) & 0xFF);
        mark[i * 4 + 2] = (uint8_t)((out[i] >> 16) & 0xFF);
        mark[i * 4 + 3] = (uint8_t)((out[i] >> 24) & 0xFF);
    }
    char* hex = town_hex_encode(mark, sizeof(mark));
    jstring ret = env->NewStringUTF(hex);
    free(hex);
    return ret;
}

// ---- Stage 15: The Drum Tower (format string) ----------------------------
// `note` is the pilgrim's scroll, `mark_hex` the Stage-14 scriptorium mark and
// `gate_tag_hex` the pilgrim's HMAC-SHA256(K1 || K2, mark).
//
// The legacy logging routine interpolates the caller-supplied scroll directly
// as the format string, so positional parameters (%n$x) reach the tower's
// resonance window where the drum key lives.
JNIEXPORT jstring JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_drumTowerFormat(
        JNIEnv *env, jobject thiz,
        jstring note, jstring mark_hex, jstring gate_tag_hex) {

    if (g_analysis_detected) return env->NewStringUTF("ERR_SHADOW_EXECUTION");

    const char* note_c = note ? env->GetStringUTFChars(note, NULL) : "";
    const char* mark_c = mark_hex ? env->GetStringUTFChars(mark_hex, NULL) : "";
    const char* gate_c = gate_tag_hex ? env->GetStringUTFChars(gate_tag_hex, NULL) : "";

    uint8_t mark[16];
    if (town_hex_decode(mark_c, mark, sizeof(mark)) != 16) {
        if (note)       env->ReleaseStringUTFChars(note, note_c);
        if (mark_hex)   env->ReleaseStringUTFChars(mark_hex, mark_c);
        if (gate_tag_hex) env->ReleaseStringUTFChars(gate_tag_hex, gate_c);
        return env->NewStringUTF("ERR_BAD_MARK");
    }

    // The tower's drum key, as it sits inside libtown.so (see config.json).
    uint8_t drum_key[32];
    town_hex_decode("7b3f9a1c5e2d8f4a0c6b9e3d7a5f1b2c4d8e1f6a3b7c9d2e5f8a4c6b1e3d7f9a",
                    drum_key, sizeof(drum_key));
    drum_tower_init(drum_key);

    int gate_ok = 0;
    char* log = drum_tower_log(note_c, mark, gate_c, &gate_ok);

    if (note)       env->ReleaseStringUTFChars(note, note_c);
    if (mark_hex)   env->ReleaseStringUTFChars(mark_hex, mark_c);
    if (gate_tag_hex) env->ReleaseStringUTFChars(gate_tag_hex, gate_c);

    if (!log) return env->NewStringUTF("ERR_TOWER_GATE");
    jstring ret = env->NewStringUTF(log);
    free(log);
    return ret;
}

// ---- Stage 16: The Courier's Ledger (protobuf wire-format abuse) ----------
JNIEXPORT jstring JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_parseCourierScroll(
        JNIEnv *env, jobject thiz,
        jbyteArray scroll, jstring drum_key_hex) {

    if (g_analysis_detected) return env->NewStringUTF("ERR_SHADOW_EXECUTION");

    jsize len = env->GetArrayLength(scroll);
    std::vector<uint8_t> buf((size_t)len);
    if (len > 0) env->GetByteArrayRegion(scroll, 0, len, (jbyte*)buf.data());

    const char* key_c = env->GetStringUTFChars(drum_key_hex, NULL);
    uint8_t drum_key[32];
    if (town_hex_decode(key_c, drum_key, sizeof(drum_key)) != 32) {
        env->ReleaseStringUTFChars(drum_key_hex, key_c);
        return env->NewStringUTF("ERR_BAD_DRUM_KEY");
    }
    env->ReleaseStringUTFChars(drum_key_hex, key_c);

    uint8_t rune[16], ctag[16];
    town_hex_decode("2a4b6c8d0e1f3a5b7c9d1e3f5a7b9c0d", rune, sizeof(rune));
    town_hex_decode("11002233445566778899aabbccddeeff", ctag, sizeof(ctag));
    courier_init(rune, ctag);

    char err[160] = {0};
    int rc = courier_parse_scroll(buf.data(), (size_t)len,
                                  drum_key, err, sizeof(err));
    if (rc != 0) return env->NewStringUTF(err);

    const uint8_t* echo = NULL;
    size_t echo_len = courier_reply(&echo);
    char* hex = town_hex_encode(echo, echo_len);
    jstring ret = env->NewStringUTF(hex);
    free(hex);
    return ret;
}

// ---- Stage 17: The Granary (TOCTOU on the mapped prefs image) -------------
// The granary ledger is held in a memory-mapped shared-preference image. The
// reader validates the pilgrim rune against the epoch, then re-reads the same
// page after the commit tap. Anything that holds the mapping open can rewrite
// the epoch inside that window.
JNIEXPORT jstring JNICALL
Java_com_vrushabhhirap_hidimbistown_native_NativeBridge_granaryQuery(
        JNIEnv *env, jobject thiz,
        jbyteArray prefs_image, jbyteArray pilgrim_rune, jint race_window_us,
        jstring scribe_rune_hex, jstring granary_secret_hex) {

    if (g_analysis_detected) return env->NewStringUTF("ERR_SHADOW_EXECUTION");

    jsize img_len = env->GetArrayLength(prefs_image);
    jsize rune_len = env->GetArrayLength(pilgrim_rune);
    std::vector<uint8_t> img((size_t)img_len);
    std::vector<uint8_t> rune((size_t)rune_len);
    if (img_len > 0)  env->GetByteArrayRegion(prefs_image, 0, img_len, (jbyte*)img.data());
    if (rune_len > 0) env->GetByteArrayRegion(pilgrim_rune, 0, rune_len, (jbyte*)rune.data());

    const char* sr = env->GetStringUTFChars(scribe_rune_hex, NULL);
    uint8_t scribe[16];
    int ok = (town_hex_decode(sr, scribe, sizeof(scribe)) == 16);
    env->ReleaseStringUTFChars(scribe_rune_hex, sr);
    if (!ok) return env->NewStringUTF("ERR_BAD_SCRIBE_RUNE");

    const char* gs = env->GetStringUTFChars(granary_secret_hex, NULL);
    uint8_t gsecret[16];
    ok = (town_hex_decode(gs, gsecret, sizeof(gsecret)) == 16);
    env->ReleaseStringUTFChars(granary_secret_hex, gs);
    if (!ok) return env->NewStringUTF("ERR_BAD_GRANARY_SECRET");

    GranaryPrefs* prefs = (GranaryPrefs*)img.data();
    prefs->magic   = GRANARY_PREFS_MAGIC;
    prefs->version = 1;
    granary_attach(prefs);
    granary_init(scribe, gsecret);

    uint8_t secret[16];
    int rc = granary_query(rune.data(), (int)race_window_us, secret);
    granary_detach();

    if (rc == 0) {
        char* hex = town_hex_encode(secret, sizeof(secret));
        jstring ret = env->NewStringUTF(hex);
        free(hex);
        return ret;
    }
    if (rc == 1) return env->NewStringUTF("ORDINARY_GRANT");
    if (rc == -2) return env->NewStringUTF("ERR_UNHOLY_RUNE");
    return env->NewStringUTF("ERR_GRANARY_UNMAPPED");
}

} // extern "C"
