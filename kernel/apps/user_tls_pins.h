#ifndef USER_TLS_PINS_H
#define USER_TLS_PINS_H

#include <stdint.h>
#include "user_tls_crypto.h"

typedef struct {
    const char* host;
    uint8_t spki_sha256[USER_TLS_SHA256_DIGEST_SIZE];
} user_tls_pin_entry_t;

int user_tls_pins_lookup(const char* host, const user_tls_pin_entry_t** out_entry);
int user_tls_pins_match_host(const char* host,
                             const uint8_t spki_sha256[USER_TLS_SHA256_DIGEST_SIZE]);
int user_tls_pins_selftest(char* detail, uint32_t detail_len);

#endif
