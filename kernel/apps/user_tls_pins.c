#include <stdint.h>
#include "user_tls_pins.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

static const char user_tls_pin_host_test[] USER_RODATA = "test.example.com";
static const char user_tls_pin_host_api[] USER_RODATA = "api.test.example.com";
static const char user_tls_pin_host_edge[] USER_RODATA = "edge.svc.example.com";
static const char user_tls_pin_host_python_com[] USER_RODATA = "www.python.com";
static const char user_tls_pin_host_python[] USER_RODATA = "www.python.org";

static const uint8_t user_tls_pin_hash_test[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0x66, 0x0e, 0x21, 0xc0, 0x5c, 0x1d, 0xdb, 0x0b,
    0x67, 0x56, 0x51, 0xce, 0x3d, 0xbf, 0xb6, 0xc1,
    0x8d, 0xcf, 0x52, 0xd3, 0x3f, 0x3a, 0x04, 0x50,
    0x62, 0x91, 0x1a, 0xd0, 0xf3, 0x9a, 0x78, 0xfc
};

static const user_tls_pin_entry_t user_tls_pin_table[] USER_RODATA = {
    { user_tls_pin_host_test,
      { 0x66, 0x0e, 0x21, 0xc0, 0x5c, 0x1d, 0xdb, 0x0b,
        0x67, 0x56, 0x51, 0xce, 0x3d, 0xbf, 0xb6, 0xc1,
        0x8d, 0xcf, 0x52, 0xd3, 0x3f, 0x3a, 0x04, 0x50,
        0x62, 0x91, 0x1a, 0xd0, 0xf3, 0x9a, 0x78, 0xfc } },
    { user_tls_pin_host_api,
      { 0x66, 0x0e, 0x21, 0xc0, 0x5c, 0x1d, 0xdb, 0x0b,
        0x67, 0x56, 0x51, 0xce, 0x3d, 0xbf, 0xb6, 0xc1,
        0x8d, 0xcf, 0x52, 0xd3, 0x3f, 0x3a, 0x04, 0x50,
        0x62, 0x91, 0x1a, 0xd0, 0xf3, 0x9a, 0x78, 0xfc } },
    { user_tls_pin_host_edge,
      { 0x66, 0x0e, 0x21, 0xc0, 0x5c, 0x1d, 0xdb, 0x0b,
        0x67, 0x56, 0x51, 0xce, 0x3d, 0xbf, 0xb6, 0xc1,
        0x8d, 0xcf, 0x52, 0xd3, 0x3f, 0x3a, 0x04, 0x50,
        0x62, 0x91, 0x1a, 0xd0, 0xf3, 0x9a, 0x78, 0xfc } },
    { user_tls_pin_host_python_com,
      { 0x48, 0x96, 0x60, 0x52, 0x5e, 0xfd, 0xe4, 0xcf,
        0xcd, 0x46, 0x5a, 0x80, 0xcf, 0xd0, 0x39, 0xe2,
        0xd9, 0x0e, 0xc4, 0xff, 0xcc, 0x5a, 0xc5, 0x86,
        0x14, 0x86, 0xcb, 0xa8, 0x4e, 0xbc, 0x6d, 0xa7 } },
    { user_tls_pin_host_python,
      { 0x01, 0xe6, 0x90, 0x70, 0xbd, 0xff, 0xa7, 0xde,
        0x1f, 0xa2, 0x0b, 0x87, 0x59, 0x30, 0x7c, 0x7b,
        0x31, 0x3d, 0x41, 0x62, 0xfa, 0x3c, 0x3e, 0x90,
        0x63, 0x96, 0xa5, 0xb9, 0x9e, 0xdb, 0xb8, 0xa0 } }
};

static const char user_tls_pins_selftest_ok[] USER_RODATA = "pins ok";
static const char user_tls_pins_selftest_lookup_fail[] USER_RODATA = "pin lookup";
static const char user_tls_pins_selftest_match_fail[] USER_RODATA = "pin match";
static const char user_tls_pins_selftest_reject_fail[] USER_RODATA = "pin reject";

static USER_CODE void user_tls_pins_copy_detail(char* detail, uint32_t detail_len, const char* text) {
    uint32_t off = 0;

    if (!detail || detail_len == 0U) return;
    if (!text) text = "";
    while (text[off] != '\0' && off + 1U < detail_len) {
        detail[off] = text[off];
        off++;
    }
    detail[off] = '\0';
}

static USER_CODE char user_tls_pins_ascii_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch - 'A' + 'a');
    return ch;
}

static USER_CODE int user_tls_pins_host_equals(const char* a, const char* b) {
    uint32_t i = 0;

    if (!a || !b) return 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (user_tls_pins_ascii_lower(a[i]) != user_tls_pins_ascii_lower(b[i])) return 0;
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

int USER_CODE user_tls_pins_lookup(const char* host, const user_tls_pin_entry_t** out_entry) {
    uint32_t count = (uint32_t)(sizeof(user_tls_pin_table) / sizeof(user_tls_pin_table[0]));

    if (out_entry) *out_entry = (const user_tls_pin_entry_t*)0;
    if (!host || host[0] == '\0') return -1;

    for (uint32_t i = 0; i < count; i++) {
        if (user_tls_pins_host_equals(host, user_tls_pin_table[i].host)) {
            if (out_entry) *out_entry = &user_tls_pin_table[i];
            return 0;
        }
    }
    return -1;
}

int USER_CODE user_tls_pins_match_host(const char* host,
                                       const uint8_t spki_sha256[USER_TLS_SHA256_DIGEST_SIZE]) {
    const user_tls_pin_entry_t* entry = (const user_tls_pin_entry_t*)0;

    if (!spki_sha256) return -1;
    if (user_tls_pins_lookup(host, &entry) != 0 || !entry) return -1;
    return user_memeq_consttime(entry->spki_sha256, spki_sha256, USER_TLS_SHA256_DIGEST_SIZE) ? 0 : -1;
}

int USER_CODE user_tls_pins_selftest(char* detail, uint32_t detail_len) {
    const user_tls_pin_entry_t* entry = (const user_tls_pin_entry_t*)0;
    uint8_t wrong_hash[USER_TLS_SHA256_DIGEST_SIZE];

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    if (user_tls_pins_lookup(user_tls_pin_host_test, &entry) != 0 || !entry) {
        user_tls_pins_copy_detail(detail, detail_len, user_tls_pins_selftest_lookup_fail);
        return -1;
    }
    if (user_tls_pins_match_host(user_tls_pin_host_api, user_tls_pin_hash_test) != 0 ||
        user_tls_pins_match_host(user_tls_pin_host_edge, user_tls_pin_hash_test) != 0) {
        user_tls_pins_copy_detail(detail, detail_len, user_tls_pins_selftest_match_fail);
        return -1;
    }

    user_memcpy(wrong_hash, user_tls_pin_hash_test, sizeof(wrong_hash));
    wrong_hash[0] ^= 0x5aU;
    if (user_tls_pins_match_host(user_tls_pin_host_test, wrong_hash) == 0 ||
        user_tls_pins_lookup("missing.example.com", &entry) == 0) {
        user_tls_pins_copy_detail(detail, detail_len, user_tls_pins_selftest_reject_fail);
        return -1;
    }

    user_tls_pins_copy_detail(detail, detail_len, user_tls_pins_selftest_ok);
    return 0;
}
