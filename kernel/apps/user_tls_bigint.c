#include <stdint.h>
#include "user_tls_bigint.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

#define memset user_memset
#define memcpy user_memcpy

static const char user_tls_bigint_selftest_ok[] USER_RODATA = "bigint ok";
static const char user_tls_bigint_selftest_add_fail[] USER_RODATA = "bigint add/sub";
static const char user_tls_bigint_selftest_mul_fail[] USER_RODATA = "bigint mul";
static const char user_tls_bigint_selftest_mod_fail[] USER_RODATA = "bigint modexp";

static USER_CODE void user_tls_bigint_copy_detail(char* detail, uint32_t detail_len, const char* text) {
    uint32_t off = 0;

    if (!detail || detail_len == 0U) return;
    if (!text) text = "";
    while (text[off] != '\0' && off + 1U < detail_len) {
        detail[off] = text[off];
        off++;
    }
    detail[off] = '\0';
}

static USER_CODE int user_tls_bigint_is_zero(const user_tls_bigint_t* value) {
    if (!value) return 1;
    for (uint32_t i = 0; i < USER_TLS_BIGINT_LIMBS; i++) {
        if (value->limbs[i] != 0U) return 0;
    }
    return 1;
}

static USER_CODE void user_tls_bigint_wide_zero(user_tls_bigint_wide_t* value) {
    if (!value) return;
    memset(value->limbs, 0, sizeof(value->limbs));
}

static USER_CODE int user_tls_bigint_wide_bitlen(const user_tls_bigint_wide_t* value) {
    if (!value) return 0;
    for (int32_t limb = (int32_t)USER_TLS_BIGINT_WIDE_LIMBS - 1; limb >= 0; limb--) {
        uint32_t word = value->limbs[limb];
        if (word != 0U) {
            int bits = 0;
            while (word != 0U) {
                word >>= 1;
                bits++;
            }
            return limb * 32 + bits;
        }
    }
    return 0;
}

static USER_CODE uint32_t user_tls_bigint_wide_get_bit(const user_tls_bigint_wide_t* value, uint32_t bit_index) {
    uint32_t limb = bit_index >> 5;
    uint32_t shift = bit_index & 31U;

    if (!value || limb >= USER_TLS_BIGINT_WIDE_LIMBS) return 0U;
    return (value->limbs[limb] >> shift) & 1U;
}

static USER_CODE void user_tls_bigint_shift_left1(user_tls_bigint_t* value) {
    uint32_t carry = 0U;

    if (!value) return;
    for (uint32_t i = 0; i < USER_TLS_BIGINT_LIMBS; i++) {
        uint32_t next = value->limbs[i] >> 31;
        value->limbs[i] = (value->limbs[i] << 1) | carry;
        carry = next;
    }
}

static USER_CODE int user_tls_bigint_modmul(user_tls_bigint_t* out, const user_tls_bigint_t* a,
                                            const user_tls_bigint_t* b, const user_tls_bigint_t* modulus) {
    user_tls_bigint_wide_t product;

    if (!out || !a || !b || !modulus) return -1;
    user_tls_bigint_mul(&product, a, b);
    return user_tls_bigint_mod_reduce(out, &product, modulus);
}

void USER_CODE user_tls_bigint_zero(user_tls_bigint_t* value) {
    if (!value) return;
    memset(value->limbs, 0, sizeof(value->limbs));
}

void USER_CODE user_tls_bigint_set_u32(user_tls_bigint_t* value, uint32_t word) {
    if (!value) return;
    user_tls_bigint_zero(value);
    value->limbs[0] = word;
}

void USER_CODE user_tls_bigint_copy(user_tls_bigint_t* dst, const user_tls_bigint_t* src) {
    if (!dst || !src) return;
    memcpy(dst->limbs, src->limbs, sizeof(dst->limbs));
}

int USER_CODE user_tls_bigint_from_be_bytes(user_tls_bigint_t* out, const uint8_t* bytes, uint32_t byte_len) {
    if (!out) return -1;
    if (!bytes && byte_len != 0U) return -1;
    if (byte_len > USER_TLS_BIGINT_MAX_BYTES) return -1;

    user_tls_bigint_zero(out);
    for (uint32_t i = 0; i < byte_len; i++) {
        uint32_t limb = i >> 2;
        uint32_t shift = (i & 3U) * 8U;
        out->limbs[limb] |= (uint32_t)bytes[byte_len - 1U - i] << shift;
    }
    return 0;
}

void USER_CODE user_tls_bigint_to_be_bytes(const user_tls_bigint_t* value, uint8_t* out, uint32_t out_len) {
    if (!out || !value) return;
    for (uint32_t i = 0; i < out_len; i++) {
        uint32_t limb = i >> 2;
        uint32_t shift = (i & 3U) * 8U;
        uint8_t byte = 0U;
        if (limb < USER_TLS_BIGINT_LIMBS) byte = (uint8_t)(value->limbs[limb] >> shift);
        out[out_len - 1U - i] = byte;
    }
}

int USER_CODE user_tls_bigint_compare(const user_tls_bigint_t* a, const user_tls_bigint_t* b) {
    if (!a || !b) return 0;
    for (int32_t i = (int32_t)USER_TLS_BIGINT_LIMBS - 1; i >= 0; i--) {
        if (a->limbs[i] > b->limbs[i]) return 1;
        if (a->limbs[i] < b->limbs[i]) return -1;
    }
    return 0;
}

int USER_CODE user_tls_bigint_add(user_tls_bigint_t* out, const user_tls_bigint_t* a, const user_tls_bigint_t* b) {
    uint64_t carry = 0U;

    if (!out || !a || !b) return -1;
    for (uint32_t i = 0; i < USER_TLS_BIGINT_LIMBS; i++) {
        uint64_t sum = (uint64_t)a->limbs[i] + (uint64_t)b->limbs[i] + carry;
        out->limbs[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    return (int)carry;
}

int USER_CODE user_tls_bigint_sub(user_tls_bigint_t* out, const user_tls_bigint_t* a, const user_tls_bigint_t* b) {
    uint64_t borrow = 0U;

    if (!out || !a || !b) return -1;
    for (uint32_t i = 0; i < USER_TLS_BIGINT_LIMBS; i++) {
        uint64_t av = (uint64_t)a->limbs[i];
        uint64_t bv = (uint64_t)b->limbs[i] + borrow;
        if (av >= bv) {
            out->limbs[i] = (uint32_t)(av - bv);
            borrow = 0U;
        } else {
            out->limbs[i] = (uint32_t)(((uint64_t)1 << 32) + av - bv);
            borrow = 1U;
        }
    }
    return (int)borrow;
}

void USER_CODE user_tls_bigint_mul(user_tls_bigint_wide_t* out, const user_tls_bigint_t* a, const user_tls_bigint_t* b) {
    if (!out || !a || !b) return;
    user_tls_bigint_wide_zero(out);

    for (uint32_t i = 0; i < USER_TLS_BIGINT_LIMBS; i++) {
        uint64_t carry = 0U;
        uint32_t k = i;

        for (uint32_t j = 0; j < USER_TLS_BIGINT_LIMBS; j++, k++) {
            uint64_t accum = (uint64_t)out->limbs[k] +
                             (uint64_t)a->limbs[i] * (uint64_t)b->limbs[j] + carry;
            out->limbs[k] = (uint32_t)accum;
            carry = accum >> 32;
        }
        while (carry != 0U && k < USER_TLS_BIGINT_WIDE_LIMBS) {
            uint64_t accum = (uint64_t)out->limbs[k] + carry;
            out->limbs[k] = (uint32_t)accum;
            carry = accum >> 32;
            k++;
        }
    }
}

int USER_CODE user_tls_bigint_mod_reduce(user_tls_bigint_t* out, const user_tls_bigint_wide_t* value,
                                         const user_tls_bigint_t* modulus) {
    user_tls_bigint_t rem;
    int bitlen;

    if (!out || !value || !modulus) return -1;
    if (user_tls_bigint_is_zero(modulus)) return -1;

    user_tls_bigint_zero(&rem);
    bitlen = user_tls_bigint_wide_bitlen(value);
    for (int32_t bit = bitlen - 1; bit >= 0; bit--) {
        user_tls_bigint_shift_left1(&rem);
        rem.limbs[0] |= user_tls_bigint_wide_get_bit(value, (uint32_t)bit);
        if (user_tls_bigint_compare(&rem, modulus) >= 0) {
            (void)user_tls_bigint_sub(&rem, &rem, modulus);
        }
    }
    user_tls_bigint_copy(out, &rem);
    return 0;
}

int USER_CODE user_tls_bigint_modexp_u32(user_tls_bigint_t* out, const user_tls_bigint_t* base,
                                         uint32_t exponent, const user_tls_bigint_t* modulus) {
    user_tls_bigint_t result;
    user_tls_bigint_t base_mod;
    user_tls_bigint_wide_t tmp_wide;

    if (!out || !base || !modulus) return -1;
    if (user_tls_bigint_is_zero(modulus)) return -1;

    user_tls_bigint_zero(&result);
    user_tls_bigint_set_u32(&result, 1U);
    user_tls_bigint_wide_zero(&tmp_wide);
    memcpy(tmp_wide.limbs, base->limbs, sizeof(base->limbs));
    if (user_tls_bigint_mod_reduce(&base_mod, &tmp_wide, modulus) != 0) return -1;

    while (exponent != 0U) {
        if ((exponent & 1U) != 0U) {
            if (user_tls_bigint_modmul(&result, &result, &base_mod, modulus) != 0) return -1;
        }
        exponent >>= 1;
        if (exponent != 0U) {
            if (user_tls_bigint_modmul(&base_mod, &base_mod, &base_mod, modulus) != 0) return -1;
        }
    }

    user_tls_bigint_copy(out, &result);
    return 0;
}

int USER_CODE user_tls_bigint_selftest(char* detail, uint32_t detail_len) {
    user_tls_bigint_t a;
    user_tls_bigint_t b;
    user_tls_bigint_t c;
    user_tls_bigint_t modulus;
    user_tls_bigint_t expected;
    user_tls_bigint_wide_t wide;

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    user_tls_bigint_set_u32(&a, 0xffffffffU);
    user_tls_bigint_set_u32(&b, 2U);
    (void)user_tls_bigint_add(&c, &a, &b);
    if (c.limbs[0] != 1U || c.limbs[1] != 1U) {
        user_tls_bigint_copy_detail(detail, detail_len, user_tls_bigint_selftest_add_fail);
        return -1;
    }
    (void)user_tls_bigint_sub(&c, &c, &b);
    if (user_tls_bigint_compare(&c, &a) != 0) {
        user_tls_bigint_copy_detail(detail, detail_len, user_tls_bigint_selftest_add_fail);
        return -1;
    }

    user_tls_bigint_set_u32(&a, 0x12345678U);
    user_tls_bigint_set_u32(&b, 0x9abcdef0U);
    user_tls_bigint_mul(&wide, &a, &b);
    if (wide.limbs[0] != 0x242d2080U || wide.limbs[1] != 0x0b00ea4eU) {
        user_tls_bigint_copy_detail(detail, detail_len, user_tls_bigint_selftest_mul_fail);
        return -1;
    }

    user_tls_bigint_set_u32(&a, 4U);
    user_tls_bigint_set_u32(&modulus, 497U);
    user_tls_bigint_set_u32(&expected, 445U);
    if (user_tls_bigint_modexp_u32(&c, &a, 13U, &modulus) != 0 ||
        user_tls_bigint_compare(&c, &expected) != 0) {
        user_tls_bigint_copy_detail(detail, detail_len, user_tls_bigint_selftest_mod_fail);
        return -1;
    }

    user_tls_bigint_copy_detail(detail, detail_len, user_tls_bigint_selftest_ok);
    return 0;
}
