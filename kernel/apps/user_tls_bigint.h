#ifndef USER_TLS_BIGINT_H
#define USER_TLS_BIGINT_H

#include <stdint.h>

#define USER_TLS_BIGINT_LIMBS 128U
#define USER_TLS_BIGINT_WIDE_LIMBS (USER_TLS_BIGINT_LIMBS * 2U)
#define USER_TLS_BIGINT_MAX_BYTES (USER_TLS_BIGINT_LIMBS * 4U)

typedef struct {
    uint32_t limbs[USER_TLS_BIGINT_LIMBS];
} user_tls_bigint_t;

typedef struct {
    uint32_t limbs[USER_TLS_BIGINT_WIDE_LIMBS];
} user_tls_bigint_wide_t;

void user_tls_bigint_zero(user_tls_bigint_t* value);
void user_tls_bigint_set_u32(user_tls_bigint_t* value, uint32_t word);
void user_tls_bigint_copy(user_tls_bigint_t* dst, const user_tls_bigint_t* src);
int user_tls_bigint_from_be_bytes(user_tls_bigint_t* out, const uint8_t* bytes, uint32_t byte_len);
void user_tls_bigint_to_be_bytes(const user_tls_bigint_t* value, uint8_t* out, uint32_t out_len);
int user_tls_bigint_compare(const user_tls_bigint_t* a, const user_tls_bigint_t* b);
int user_tls_bigint_add(user_tls_bigint_t* out, const user_tls_bigint_t* a, const user_tls_bigint_t* b);
int user_tls_bigint_sub(user_tls_bigint_t* out, const user_tls_bigint_t* a, const user_tls_bigint_t* b);
void user_tls_bigint_mul(user_tls_bigint_wide_t* out, const user_tls_bigint_t* a, const user_tls_bigint_t* b);
int user_tls_bigint_mod_reduce(user_tls_bigint_t* out, const user_tls_bigint_wide_t* value,
                               const user_tls_bigint_t* modulus);
int user_tls_bigint_modexp_u32(user_tls_bigint_t* out, const user_tls_bigint_t* base,
                               uint32_t exponent, const user_tls_bigint_t* modulus);
int user_tls_bigint_selftest(char* detail, uint32_t detail_len);

#endif
