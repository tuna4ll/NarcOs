#include <stdint.h>
#include "user_tls_crypto.h"
#include "user_tls_bigint.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

#define memset user_memset
#define memcpy user_memcpy
#define memcmp user_memcmp
#define strlen user_strlen

typedef struct {
    user_tls_sha256_ctx_t inner;
    user_tls_sha256_ctx_t outer;
} user_tls_hmac_sha256_ctx_t;

typedef int64_t user_tls_x25519_fe[16];

static const uint8_t user_tls_aes_sbox[256] USER_RODATA = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};

static const uint8_t user_tls_aes_rcon[10] USER_RODATA = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36
};

static const uint32_t user_tls_sha256_k[64] USER_RODATA = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static const uint8_t user_tls_sha256_empty_expected[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

static const uint8_t user_tls_sha256_abc_expected[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
    0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
    0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
};

static const uint8_t user_tls_sha256_abcdef_expected[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xbe, 0xf5, 0x7e, 0xc7, 0xf5, 0x3a, 0x6d, 0x40,
    0xbe, 0xb6, 0x40, 0xa7, 0x80, 0xa6, 0x39, 0xc8,
    0x3b, 0xc2, 0x9a, 0xc8, 0xa9, 0x81, 0x6f, 0x1f,
    0xc6, 0xc5, 0xc6, 0xdc, 0xd9, 0x3c, 0x47, 0x21
};

static const uint8_t user_tls_hmac_tc1_key[20] USER_RODATA = {
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
};

static const uint8_t user_tls_hmac_tc1_data[] USER_RODATA = {
    'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e'
};

static const uint8_t user_tls_hmac_tc1_expected[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
    0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
    0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
    0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
};

static const uint8_t user_tls_hmac_tc2_key[] USER_RODATA = {
    'J', 'e', 'f', 'e'
};

static const uint8_t user_tls_hmac_tc2_data[] USER_RODATA = {
    'w', 'h', 'a', 't', ' ', 'd', 'o', ' ', 'y', 'a', ' ', 'w', 'a', 'n', 't', ' ',
    'f', 'o', 'r', ' ', 'n', 'o', 't', 'h', 'i', 'n', 'g', '?'
};

static const uint8_t user_tls_hmac_tc2_expected[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e,
    0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
    0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83,
    0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43
};

static const uint8_t user_tls_hkdf_tc1_ikm[22] USER_RODATA = {
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
};

static const uint8_t user_tls_hkdf_tc1_salt[13] USER_RODATA = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
};

static const uint8_t user_tls_hkdf_tc1_info[10] USER_RODATA = {
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9
};

static const uint8_t user_tls_hkdf_tc1_prk[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf,
    0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
    0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31,
    0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5
};

static const uint8_t user_tls_hkdf_tc1_okm[42] USER_RODATA = {
    0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a, 0x90, 0x43, 0x4f,
    0x64, 0xd0, 0x36, 0x2f, 0x2a, 0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a,
    0x5a, 0x4c, 0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf, 0x34,
    0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18, 0x58, 0x65
};

static const uint8_t user_tls_expand_label_secret[32] USER_RODATA = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t user_tls_expand_label_context[6] USER_RODATA = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5
};

static const uint8_t user_tls_expand_label_expected[16] USER_RODATA = {
    0x93, 0x79, 0xb9, 0x20, 0xa0, 0x54, 0x8e, 0xe6,
    0x25, 0x04, 0x19, 0x0f, 0x4b, 0x07, 0x6f, 0xa1
};

static const uint8_t user_tls_aes_block_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
};

static const uint8_t user_tls_aes_block_plain[USER_TLS_AES_BLOCK_SIZE] USER_RODATA = {
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
};

static const uint8_t user_tls_aes_block_expected[USER_TLS_AES_BLOCK_SIZE] USER_RODATA = {
    0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
    0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a
};

static const uint8_t user_tls_gcm_zero_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t user_tls_gcm_zero_iv[USER_TLS_GCM_IV_SIZE] USER_RODATA = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t user_tls_gcm_empty_tag[USER_TLS_GCM_TAG_SIZE] USER_RODATA = {
    0x58, 0xe2, 0xfc, 0xce, 0xfa, 0x7e, 0x30, 0x61,
    0x36, 0x7f, 0x1d, 0x57, 0xa4, 0xe7, 0x45, 0x5a
};

static const uint8_t user_tls_gcm_zero_plain[USER_TLS_AES_BLOCK_SIZE] USER_RODATA = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t user_tls_gcm_zero_cipher[USER_TLS_AES_BLOCK_SIZE] USER_RODATA = {
    0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92,
    0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78
};

static const uint8_t user_tls_gcm_zero_tag[USER_TLS_GCM_TAG_SIZE] USER_RODATA = {
    0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd,
    0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf
};

static const uint8_t user_tls_gcm_long_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
    0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08
};

static const uint8_t user_tls_gcm_long_iv[USER_TLS_GCM_IV_SIZE] USER_RODATA = {
    0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce,
    0xdb, 0xad, 0xde, 0xca, 0xf8, 0x88
};

static const uint8_t user_tls_gcm_long_aad[20] USER_RODATA = {
    0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
    0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
    0xab, 0xad, 0xda, 0xd2
};

static const uint8_t user_tls_gcm_long_plain[60] USER_RODATA = {
    0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5, 0xa5, 0x59, 0x09, 0xc5,
    0xaf, 0xf5, 0x26, 0x9a, 0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
    0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72, 0x1c, 0x3c, 0x0c, 0x95,
    0x95, 0x68, 0x09, 0x53, 0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
    0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57, 0xba, 0x63, 0x7b, 0x39
};

static const uint8_t user_tls_gcm_long_cipher[60] USER_RODATA = {
    0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24, 0x4b, 0x72, 0x21, 0xb7,
    0x84, 0xd0, 0xd4, 0x9c, 0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
    0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e, 0x21, 0xd5, 0x14, 0xb2,
    0x54, 0x66, 0x93, 0x1c, 0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
    0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97, 0x3d, 0x58, 0xe0, 0x91
};

static const uint8_t user_tls_gcm_long_tag[USER_TLS_GCM_TAG_SIZE] USER_RODATA = {
    0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb,
    0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a, 0x47
};

static const user_tls_x25519_fe user_tls_x25519_a24 = {
    0xdb41, 0x0001, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const uint8_t user_tls_x25519_basepoint[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t user_tls_x25519_scalar_tc1[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0xa5, 0x46, 0xe3, 0x6b, 0xf0, 0x52, 0x7c, 0x9d,
    0x3b, 0x16, 0x15, 0x4b, 0x82, 0x46, 0x5e, 0xdd,
    0x62, 0x14, 0x4c, 0x0a, 0xc1, 0xfc, 0x5a, 0x18,
    0x50, 0x6a, 0x22, 0x44, 0xba, 0x44, 0x9a, 0xc4
};

static const uint8_t user_tls_x25519_u_tc1[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0xe6, 0xdb, 0x68, 0x67, 0x58, 0x30, 0x30, 0xdb,
    0x35, 0x94, 0xc1, 0xa4, 0x24, 0xb1, 0x5f, 0x7c,
    0x72, 0x66, 0x24, 0xec, 0x26, 0xb3, 0x35, 0x3b,
    0x10, 0xa9, 0x03, 0xa6, 0xd0, 0xab, 0x1c, 0x4c
};

static const uint8_t user_tls_x25519_out_tc1[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0xc3, 0xda, 0x55, 0x37, 0x9d, 0xe9, 0xc6, 0x90,
    0x8e, 0x94, 0xea, 0x4d, 0xf2, 0x8d, 0x08, 0x4f,
    0x32, 0xec, 0xcf, 0x03, 0x49, 0x1c, 0x71, 0xf7,
    0x54, 0xb4, 0x07, 0x55, 0x77, 0xa2, 0x85, 0x52
};

static const uint8_t user_tls_x25519_scalar_tc2[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x4b, 0x66, 0xe9, 0xd4, 0xd1, 0xb4, 0x67, 0x3c,
    0x5a, 0xd2, 0x26, 0x91, 0x95, 0x7d, 0x6a, 0xf5,
    0xc1, 0x1b, 0x64, 0x21, 0xe0, 0xea, 0x01, 0xd4,
    0x2c, 0xa4, 0x16, 0x9e, 0x79, 0x18, 0xba, 0x0d
};

static const uint8_t user_tls_x25519_u_tc2[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0xe5, 0x21, 0x0f, 0x12, 0x78, 0x68, 0x11, 0xd3,
    0xf4, 0xb7, 0x95, 0x9d, 0x05, 0x38, 0xae, 0x2c,
    0x31, 0xdb, 0xe7, 0x10, 0x6f, 0xc0, 0x3c, 0x3e,
    0xfc, 0x4c, 0xd5, 0x49, 0xc7, 0x15, 0xa4, 0x93
};

static const uint8_t user_tls_x25519_out_tc2[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x95, 0xcb, 0xde, 0x94, 0x76, 0xe8, 0x90, 0x7d,
    0x7a, 0xad, 0xe4, 0x5c, 0xb4, 0xb8, 0x73, 0xf8,
    0x8b, 0x59, 0x5a, 0x68, 0x79, 0x9f, 0xa1, 0x52,
    0xe6, 0xf8, 0xf7, 0x64, 0x7a, 0xac, 0x79, 0x57
};

static const uint8_t user_tls_x25519_one_iter[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x42, 0x2c, 0x8e, 0x7a, 0x62, 0x27, 0xd7, 0xbc,
    0xa1, 0x35, 0x0b, 0x3e, 0x2b, 0xb7, 0x27, 0x9f,
    0x78, 0x97, 0xb8, 0x7b, 0xb6, 0x85, 0x4b, 0x78,
    0x3c, 0x60, 0xe8, 0x03, 0x11, 0xae, 0x30, 0x79
};

static const uint8_t user_tls_x25519_alice_priv[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d,
    0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
    0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a,
    0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a
};

static const uint8_t user_tls_x25519_alice_pub[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54,
    0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
    0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4,
    0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a
};

static const uint8_t user_tls_x25519_bob_priv[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x5d, 0xab, 0x08, 0x7e, 0x62, 0x4a, 0x8a, 0x4b,
    0x79, 0xe1, 0x7f, 0x8b, 0x83, 0x80, 0x0e, 0xe6,
    0x6f, 0x3b, 0xb1, 0x29, 0x26, 0x18, 0xb6, 0xfd,
    0x1c, 0x2f, 0x8b, 0x27, 0xff, 0x88, 0xe0, 0xeb
};

static const uint8_t user_tls_x25519_bob_pub[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0xde, 0x9e, 0xdb, 0x7d, 0x7b, 0x7d, 0xc1, 0xb4,
    0xd3, 0x5b, 0x61, 0xc2, 0xec, 0xe4, 0x35, 0x37,
    0x3f, 0x83, 0x43, 0xc8, 0x5b, 0x78, 0x67, 0x4d,
    0xad, 0xfc, 0x7e, 0x14, 0x6f, 0x88, 0x2b, 0x4f
};

static const uint8_t user_tls_x25519_shared[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0x4a, 0x5d, 0x9d, 0x5b, 0xa4, 0xce, 0x2d, 0xe1,
    0x72, 0x8e, 0x3b, 0xf4, 0x80, 0x35, 0x0f, 0x25,
    0xe0, 0x7e, 0x21, 0xc9, 0x47, 0xd1, 0x9e, 0x33,
    0x76, 0xf0, 0x9b, 0x3c, 0x1e, 0x16, 0x17, 0x42
};

static const uint8_t user_tls_rsapss_modulus[128] USER_RODATA = {
    0xdb, 0x59, 0x6f, 0x13, 0xea, 0xb7, 0xcc, 0x3c, 0x70, 0xcb, 0xe9, 0xa6, 0xef, 0xf9, 0x95, 0x53,
    0x5b, 0xc5, 0xba, 0xd5, 0x5e, 0x4b, 0x2e, 0x6a, 0x5e, 0x99, 0xd7, 0x58, 0x63, 0x3a, 0x32, 0xd9,
    0x37, 0xd8, 0xc5, 0xb5, 0x8b, 0xc0, 0x8d, 0xbf, 0xe8, 0xad, 0x73, 0x2d, 0xf2, 0xf6, 0xe0, 0xe8,
    0x51, 0x23, 0xc7, 0x45, 0xa3, 0xbf, 0xb7, 0x45, 0x13, 0xb4, 0x40, 0x3a, 0x0c, 0xba, 0x24, 0xf8,
    0x78, 0xd8, 0x9c, 0xde, 0xf0, 0xa9, 0xe8, 0x8c, 0x9b, 0xaa, 0x98, 0x8b, 0xa8, 0xf3, 0xbe, 0x4a,
    0x67, 0x67, 0xeb, 0x1e, 0xf3, 0xa3, 0x81, 0x0a, 0x3f, 0x26, 0x93, 0xc4, 0xcc, 0x31, 0x5b, 0xca,
    0xbb, 0x93, 0x2f, 0xb3, 0x2a, 0xec, 0x1f, 0xe7, 0xf0, 0xd9, 0x8e, 0xbd, 0xd4, 0xed, 0x6f, 0xf1,
    0xda, 0x76, 0x77, 0x5b, 0x10, 0x0e, 0x4d, 0xa4, 0xb7, 0x34, 0x29, 0xde, 0xa8, 0xb3, 0x0b, 0xf1
};

static const uint8_t user_tls_rsapss_signature[128] USER_RODATA = {
    0x1a, 0x56, 0xaa, 0xf7, 0xc2, 0xd3, 0xbb, 0xbf, 0xbb, 0xda, 0xc6, 0xa1, 0x88, 0x9f, 0x76, 0xee,
    0x0d, 0x89, 0x4b, 0xed, 0x6c, 0x97, 0xa3, 0x25, 0x47, 0x3d, 0xdb, 0x83, 0x74, 0xb1, 0x91, 0xa0,
    0xb9, 0x94, 0x8f, 0x8f, 0x2a, 0x3f, 0xab, 0x6d, 0x5d, 0x58, 0x8e, 0xe9, 0x21, 0x99, 0x0a, 0xcc,
    0x40, 0xe0, 0xad, 0x42, 0xbd, 0x7f, 0x61, 0xa7, 0x91, 0x56, 0xca, 0x7b, 0xcc, 0x6a, 0xfe, 0x73,
    0x9f, 0xf8, 0x4a, 0x79, 0x34, 0x00, 0x66, 0x2f, 0x15, 0x61, 0x9e, 0x8d, 0xde, 0x4a, 0x46, 0x9b,
    0x1e, 0x40, 0x91, 0xc8, 0xc9, 0x96, 0x60, 0xb4, 0x3a, 0xb3, 0x7a, 0x2b, 0x7b, 0x9c, 0xe7, 0xed,
    0x19, 0xce, 0xc0, 0x39, 0xfd, 0x53, 0xd7, 0xc9, 0x47, 0xfc, 0x45, 0x77, 0xa6, 0x60, 0x54, 0x53,
    0x56, 0x75, 0x6c, 0xae, 0x7c, 0x28, 0xc9, 0x06, 0x63, 0xaf, 0x2b, 0x76, 0x62, 0xb5, 0xe2, 0xaf
};

static const uint8_t user_tls_rsapss_message[] USER_RODATA = {
    'N', 'a', 'r', 'c', 'O', 's', ' ', 'T', 'L', 'S', ' ', 'R', 'S', 'A', '-', 'P',
    'S', 'S', ' ', 's', 'e', 'l', 'f', 't', 'e', 's', 't'
};

static const char user_tls_expand_label_name[] USER_RODATA = "test";

static const char user_tls_selftest_ok[] USER_RODATA = "sha256+hmac+hkdf+transcript ok";
static const char user_tls_selftest_aes_ok[] USER_RODATA = "aes128+gcm ok";
static const char user_tls_selftest_sha256_empty_fail[] USER_RODATA = "sha256 empty vector";
static const char user_tls_selftest_sha256_abc_fail[] USER_RODATA = "sha256 abc vector";
static const char user_tls_selftest_hmac_tc1_fail[] USER_RODATA = "hmac rfc4231 tc1";
static const char user_tls_selftest_hmac_tc2_fail[] USER_RODATA = "hmac rfc4231 tc2";
static const char user_tls_selftest_hkdf_extract_fail[] USER_RODATA = "hkdf extract tc1";
static const char user_tls_selftest_hkdf_expand_fail[] USER_RODATA = "hkdf expand tc1";
static const char user_tls_selftest_label_fail[] USER_RODATA = "hkdf expand label";
static const char user_tls_selftest_transcript_fail[] USER_RODATA = "transcript hash";
static const char user_tls_selftest_invalid[] USER_RODATA = "invalid input";
static const char user_tls_selftest_aes_block_fail[] USER_RODATA = "aes-128 block vector";
static const char user_tls_selftest_gcm_empty_fail[] USER_RODATA = "gcm empty vector";
static const char user_tls_selftest_gcm_zero_fail[] USER_RODATA = "gcm zero block";
static const char user_tls_selftest_gcm_long_fail[] USER_RODATA = "gcm aad vector";
static const char user_tls_selftest_gcm_decrypt_fail[] USER_RODATA = "gcm decrypt";
static const char user_tls_selftest_gcm_bad_tag_fail[] USER_RODATA = "gcm bad tag reject";
static const char user_tls_selftest_x25519_ok[] USER_RODATA = "x25519 rfc7748 ok";
static const char user_tls_selftest_x25519_clamp_fail[] USER_RODATA = "x25519 clamp";
static const char user_tls_selftest_x25519_tc1_fail[] USER_RODATA = "x25519 vector 1";
static const char user_tls_selftest_x25519_tc2_fail[] USER_RODATA = "x25519 vector 2";
static const char user_tls_selftest_x25519_iter_fail[] USER_RODATA = "x25519 one iteration";
static const char user_tls_selftest_x25519_pub_fail[] USER_RODATA = "x25519 public key";
static const char user_tls_selftest_x25519_shared_fail[] USER_RODATA = "x25519 shared secret";
static const char user_tls_selftest_rsapss_ok[] USER_RODATA = "bigint+rsa-pss ok";
static const char user_tls_selftest_rsapss_bigint_fail[] USER_RODATA = "bigint selftest";
static const char user_tls_selftest_rsapss_verify_fail[] USER_RODATA = "rsa-pss verify";
static const char user_tls_selftest_rsapss_reject_fail[] USER_RODATA = "rsa-pss reject tamper";

static USER_CODE uint32_t user_tls_rotr32(uint32_t value, uint32_t shift) {
    return (value >> shift) | (value << (32U - shift));
}

static USER_CODE uint32_t user_tls_load_be32(const uint8_t* src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static USER_CODE void user_tls_store_be32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static USER_CODE void user_tls_store_be64(uint8_t* dst, uint64_t value) {
    for (uint32_t i = 0; i < 8U; i++) {
        dst[i] = (uint8_t)(value >> (56U - i * 8U));
    }
}

static USER_CODE void user_tls_copy_detail(char* detail, uint32_t detail_len, const char* text) {
    uint32_t off = 0;

    if (!detail || detail_len == 0U) return;
    if (!text) text = "";
    while (text[off] != '\0' && off + 1U < detail_len) {
        detail[off] = text[off];
        off++;
    }
    detail[off] = '\0';
}

static USER_CODE uint8_t user_tls_xtime(uint8_t value) {
    return (uint8_t)((value << 1) ^ ((value & 0x80U) != 0U ? 0x1bU : 0x00U));
}

static USER_CODE void user_tls_xor_block(uint8_t* dst, const uint8_t* src) {
    for (uint32_t i = 0; i < USER_TLS_AES_BLOCK_SIZE; i++) dst[i] ^= src[i];
}

static USER_CODE void user_tls_inc32(uint8_t counter[USER_TLS_AES_BLOCK_SIZE]) {
    for (int i = USER_TLS_AES_BLOCK_SIZE - 1; i >= 12; i--) {
        counter[i] = (uint8_t)(counter[i] + 1U);
        if (counter[i] != 0U) break;
    }
}

static USER_CODE void user_tls_aes_key_expand(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                              uint8_t round_keys[176]) {
    uint32_t bytes_generated = USER_TLS_AES128_KEY_SIZE;
    uint32_t rcon_index = 0U;
    uint8_t temp[4];

    memcpy(round_keys, key, USER_TLS_AES128_KEY_SIZE);
    while (bytes_generated < 176U) {
        for (uint32_t i = 0; i < 4U; i++) temp[i] = round_keys[bytes_generated - 4U + i];
        if ((bytes_generated % USER_TLS_AES128_KEY_SIZE) == 0U) {
            uint8_t t = temp[0];
            temp[0] = user_tls_aes_sbox[temp[1]] ^ user_tls_aes_rcon[rcon_index++];
            temp[1] = user_tls_aes_sbox[temp[2]];
            temp[2] = user_tls_aes_sbox[temp[3]];
            temp[3] = user_tls_aes_sbox[t];
        }
        for (uint32_t i = 0; i < 4U; i++) {
            round_keys[bytes_generated] = (uint8_t)(round_keys[bytes_generated - USER_TLS_AES128_KEY_SIZE] ^ temp[i]);
            bytes_generated++;
        }
    }
}

static USER_CODE void user_tls_aes_add_round_key(uint8_t state[USER_TLS_AES_BLOCK_SIZE], const uint8_t* round_key) {
    for (uint32_t i = 0; i < USER_TLS_AES_BLOCK_SIZE; i++) state[i] ^= round_key[i];
}

static USER_CODE void user_tls_aes_sub_bytes(uint8_t state[USER_TLS_AES_BLOCK_SIZE]) {
    for (uint32_t i = 0; i < USER_TLS_AES_BLOCK_SIZE; i++) state[i] = user_tls_aes_sbox[state[i]];
}

static USER_CODE void user_tls_aes_shift_rows(uint8_t state[USER_TLS_AES_BLOCK_SIZE]) {
    uint8_t temp;

    temp = state[1];
    state[1] = state[5];
    state[5] = state[9];
    state[9] = state[13];
    state[13] = temp;

    temp = state[2];
    state[2] = state[10];
    state[10] = temp;
    temp = state[6];
    state[6] = state[14];
    state[14] = temp;

    temp = state[15];
    state[15] = state[11];
    state[11] = state[7];
    state[7] = state[3];
    state[3] = temp;
}

static USER_CODE void user_tls_aes_mix_columns(uint8_t state[USER_TLS_AES_BLOCK_SIZE]) {
    for (uint32_t col = 0; col < 4U; col++) {
        uint32_t base = col * 4U;
        uint8_t s0 = state[base + 0U];
        uint8_t s1 = state[base + 1U];
        uint8_t s2 = state[base + 2U];
        uint8_t s3 = state[base + 3U];
        uint8_t x0 = user_tls_xtime(s0);
        uint8_t x1 = user_tls_xtime(s1);
        uint8_t x2 = user_tls_xtime(s2);
        uint8_t x3 = user_tls_xtime(s3);

        state[base + 0U] = (uint8_t)(x0 ^ (x1 ^ s1) ^ s2 ^ s3);
        state[base + 1U] = (uint8_t)(s0 ^ x1 ^ (x2 ^ s2) ^ s3);
        state[base + 2U] = (uint8_t)(s0 ^ s1 ^ x2 ^ (x3 ^ s3));
        state[base + 3U] = (uint8_t)((x0 ^ s0) ^ s1 ^ s2 ^ x3);
    }
}

static USER_CODE void user_tls_gcm_multiply(const uint8_t x[USER_TLS_AES_BLOCK_SIZE],
                                            const uint8_t y[USER_TLS_AES_BLOCK_SIZE],
                                            uint8_t out[USER_TLS_AES_BLOCK_SIZE]) {
    uint8_t z[USER_TLS_AES_BLOCK_SIZE];
    uint8_t v[USER_TLS_AES_BLOCK_SIZE];

    memset(z, 0, sizeof(z));
    memcpy(v, y, sizeof(v));
    for (uint32_t byte_index = 0; byte_index < USER_TLS_AES_BLOCK_SIZE; byte_index++) {
        uint8_t current = x[byte_index];
        for (uint32_t bit = 0; bit < 8U; bit++) {
            if ((current & 0x80U) != 0U) user_tls_xor_block(z, v);
            current <<= 1;

            {
                uint8_t lsb = (uint8_t)(v[15] & 0x01U);
                for (int i = USER_TLS_AES_BLOCK_SIZE - 1; i > 0; i--) {
                    v[i] = (uint8_t)((v[i] >> 1) | ((v[i - 1] & 0x01U) << 7));
                }
                v[0] >>= 1;
                if (lsb != 0U) v[0] ^= 0xe1U;
            }
        }
    }
    memcpy(out, z, USER_TLS_AES_BLOCK_SIZE);
}

static USER_CODE void user_tls_gcm_ghash_update_block(uint8_t state[USER_TLS_AES_BLOCK_SIZE],
                                                      const uint8_t hash_key[USER_TLS_AES_BLOCK_SIZE],
                                                      const uint8_t block[USER_TLS_AES_BLOCK_SIZE]) {
    uint8_t product[USER_TLS_AES_BLOCK_SIZE];

    user_tls_xor_block(state, block);
    user_tls_gcm_multiply(state, hash_key, product);
    memcpy(state, product, USER_TLS_AES_BLOCK_SIZE);
}

static USER_CODE void user_tls_gcm_ghash_range(uint8_t state[USER_TLS_AES_BLOCK_SIZE],
                                               const uint8_t hash_key[USER_TLS_AES_BLOCK_SIZE],
                                               const uint8_t* data, uint32_t len) {
    uint8_t block[USER_TLS_AES_BLOCK_SIZE];

    if (!data && len != 0U) return;
    while (len >= USER_TLS_AES_BLOCK_SIZE) {
        user_tls_gcm_ghash_update_block(state, hash_key, data);
        data += USER_TLS_AES_BLOCK_SIZE;
        len -= USER_TLS_AES_BLOCK_SIZE;
    }
    if (len != 0U) {
        memset(block, 0, sizeof(block));
        memcpy(block, data, len);
        user_tls_gcm_ghash_update_block(state, hash_key, block);
    }
}

static USER_CODE void user_tls_gcm_compute_tag(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                               const uint8_t iv[USER_TLS_GCM_IV_SIZE],
                                               const void* aad, uint32_t aad_len,
                                               const uint8_t* ciphertext, uint32_t ciphertext_len,
                                               uint8_t out_tag[USER_TLS_GCM_TAG_SIZE]) {
    uint8_t hash_key[USER_TLS_AES_BLOCK_SIZE];
    uint8_t y[USER_TLS_AES_BLOCK_SIZE];
    uint8_t j0[USER_TLS_AES_BLOCK_SIZE];
    uint8_t len_block[USER_TLS_AES_BLOCK_SIZE];

    memset(hash_key, 0, sizeof(hash_key));
    user_tls_aes128_encrypt_block(key, hash_key, hash_key);
    memset(y, 0, sizeof(y));
    if (aad_len != 0U) user_tls_gcm_ghash_range(y, hash_key, (const uint8_t*)aad, aad_len);
    if (ciphertext_len != 0U) user_tls_gcm_ghash_range(y, hash_key, ciphertext, ciphertext_len);

    memset(len_block, 0, sizeof(len_block));
    user_tls_store_be64(len_block, (uint64_t)aad_len * 8U);
    user_tls_store_be64(len_block + 8U, (uint64_t)ciphertext_len * 8U);
    user_tls_gcm_ghash_update_block(y, hash_key, len_block);

    memcpy(j0, iv, USER_TLS_GCM_IV_SIZE);
    j0[12] = 0x00U;
    j0[13] = 0x00U;
    j0[14] = 0x00U;
    j0[15] = 0x01U;
    user_tls_aes128_encrypt_block(key, j0, out_tag);
    user_tls_xor_block(out_tag, y);
}

static USER_CODE void user_tls_x25519_fe_copy(user_tls_x25519_fe dst, const user_tls_x25519_fe src) {
    for (uint32_t i = 0; i < 16U; i++) dst[i] = src[i];
}

static USER_CODE void user_tls_x25519_fe_set_small(user_tls_x25519_fe dst, int64_t value) {
    dst[0] = value;
    for (uint32_t i = 1; i < 16U; i++) dst[i] = 0;
}

static USER_CODE void user_tls_x25519_fe_add(user_tls_x25519_fe out,
                                             const user_tls_x25519_fe a, const user_tls_x25519_fe b) {
    for (uint32_t i = 0; i < 16U; i++) out[i] = a[i] + b[i];
}

static USER_CODE void user_tls_x25519_fe_sub(user_tls_x25519_fe out,
                                             const user_tls_x25519_fe a, const user_tls_x25519_fe b) {
    for (uint32_t i = 0; i < 16U; i++) out[i] = a[i] - b[i];
}

static USER_CODE void user_tls_x25519_fe_carry(user_tls_x25519_fe out) {
    for (uint32_t i = 0; i < 16U; i++) {
        int64_t carry;
        out[i] += (int64_t)1 << 16;
        carry = out[i] >> 16;
        out[(i + 1U) * (i < 15U)] += carry - 1 + 37 * (carry - 1) * (i == 15U);
        out[i] -= carry << 16;
    }
}

static USER_CODE void user_tls_x25519_fe_swap(user_tls_x25519_fe p,
                                              user_tls_x25519_fe q, uint8_t bit) {
    int64_t mask = -(int64_t)bit;

    for (uint32_t i = 0; i < 16U; i++) {
        int64_t t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static USER_CODE void user_tls_x25519_fe_unpack(user_tls_x25519_fe out,
                                                const uint8_t input[USER_TLS_X25519_KEY_SIZE]) {
    for (uint32_t i = 0; i < 16U; i++) {
        out[i] = (int64_t)input[2U * i] | ((int64_t)input[2U * i + 1U] << 8);
    }
    out[15] &= 0x7fff;
}

static USER_CODE void user_tls_x25519_fe_pack(uint8_t out[USER_TLS_X25519_KEY_SIZE],
                                              const user_tls_x25519_fe input) {
    user_tls_x25519_fe m;
    user_tls_x25519_fe t;

    user_tls_x25519_fe_copy(t, input);
    user_tls_x25519_fe_carry(t);
    user_tls_x25519_fe_carry(t);
    user_tls_x25519_fe_carry(t);

    for (uint32_t j = 0; j < 2U; j++) {
        int64_t b;

        m[0] = t[0] - 0xffed;
        for (uint32_t i = 1; i < 15U; i++) {
            m[i] = t[i] - 0xffff - ((m[i - 1U] >> 16) & 1);
            m[i - 1U] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        user_tls_x25519_fe_swap(t, m, (uint8_t)(1U - (uint8_t)b));
    }

    for (uint32_t i = 0; i < 16U; i++) {
        out[2U * i] = (uint8_t)(t[i] & 0xff);
        out[2U * i + 1U] = (uint8_t)((t[i] >> 8) & 0xff);
    }
}

static USER_CODE void user_tls_x25519_fe_mul(user_tls_x25519_fe out,
                                             const user_tls_x25519_fe a, const user_tls_x25519_fe b) {
    int64_t t[31];

    memset(t, 0, sizeof(t));
    for (uint32_t i = 0; i < 16U; i++) {
        for (uint32_t j = 0; j < 16U; j++) {
            t[i + j] += a[i] * b[j];
        }
    }
    for (uint32_t i = 0; i < 15U; i++) t[i] += 38 * t[i + 16U];
    for (uint32_t i = 0; i < 16U; i++) out[i] = t[i];
    user_tls_x25519_fe_carry(out);
    user_tls_x25519_fe_carry(out);
}

static USER_CODE void user_tls_x25519_fe_sq(user_tls_x25519_fe out, const user_tls_x25519_fe a) {
    user_tls_x25519_fe_mul(out, a, a);
}

static USER_CODE void user_tls_x25519_fe_inv(user_tls_x25519_fe out, const user_tls_x25519_fe input) {
    user_tls_x25519_fe c;

    user_tls_x25519_fe_copy(c, input);
    for (int32_t i = 253; i >= 0; i--) {
        user_tls_x25519_fe_sq(c, c);
        if (i != 2 && i != 4) user_tls_x25519_fe_mul(c, c, input);
    }
    user_tls_x25519_fe_copy(out, c);
}

static USER_CODE void user_tls_sha256_transform(user_tls_sha256_ctx_t* ctx, const uint8_t block[64]) {
    uint32_t schedule[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;

    for (uint32_t i = 0; i < 16U; i++) {
        schedule[i] = user_tls_load_be32(block + i * 4U);
    }
    for (uint32_t i = 16U; i < 64U; i++) {
        uint32_t s0 = user_tls_rotr32(schedule[i - 15U], 7U) ^
                      user_tls_rotr32(schedule[i - 15U], 18U) ^
                      (schedule[i - 15U] >> 3);
        uint32_t s1 = user_tls_rotr32(schedule[i - 2U], 17U) ^
                      user_tls_rotr32(schedule[i - 2U], 19U) ^
                      (schedule[i - 2U] >> 10);
        schedule[i] = schedule[i - 16U] + s0 + schedule[i - 7U] + s1;
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0; i < 64U; i++) {
        uint32_t s1 = user_tls_rotr32(e, 6U) ^ user_tls_rotr32(e, 11U) ^ user_tls_rotr32(e, 25U);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + choice + user_tls_sha256_k[i] + schedule[i];
        uint32_t s0 = user_tls_rotr32(a, 2U) ^ user_tls_rotr32(a, 13U) ^ user_tls_rotr32(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void USER_CODE user_tls_sha256_init(user_tls_sha256_ctx_t* ctx) {
    if (!ctx) return;

    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
    ctx->total_len = 0U;
    ctx->block_len = 0U;
    for (uint32_t i = 0; i < USER_TLS_SHA256_BLOCK_SIZE; i++) ctx->block[i] = 0U;
}

void USER_CODE user_tls_sha256_update(user_tls_sha256_ctx_t* ctx, const void* data, uint32_t len) {
    const uint8_t* src = (const uint8_t*)data;

    if (!ctx || (!src && len != 0U)) return;
    if (len == 0U) return;

    ctx->total_len += (uint64_t)len;

    if (ctx->block_len != 0U) {
        uint32_t copy_len = USER_TLS_SHA256_BLOCK_SIZE - ctx->block_len;
        if (copy_len > len) copy_len = len;
        memcpy(ctx->block + ctx->block_len, src, copy_len);
        ctx->block_len += copy_len;
        src += copy_len;
        len -= copy_len;
        if (ctx->block_len == USER_TLS_SHA256_BLOCK_SIZE) {
            user_tls_sha256_transform(ctx, ctx->block);
            ctx->block_len = 0U;
        }
    }

    while (len >= USER_TLS_SHA256_BLOCK_SIZE) {
        user_tls_sha256_transform(ctx, src);
        src += USER_TLS_SHA256_BLOCK_SIZE;
        len -= USER_TLS_SHA256_BLOCK_SIZE;
    }

    if (len != 0U) {
        memcpy(ctx->block, src, len);
        ctx->block_len = len;
    }
}

void USER_CODE user_tls_sha256_final(user_tls_sha256_ctx_t* ctx, uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]) {
    uint64_t bit_len;

    if (!ctx || !out_digest) return;

    bit_len = ctx->total_len * 8U;
    ctx->block[ctx->block_len++] = 0x80U;

    if (ctx->block_len > 56U) {
        while (ctx->block_len < USER_TLS_SHA256_BLOCK_SIZE) ctx->block[ctx->block_len++] = 0U;
        user_tls_sha256_transform(ctx, ctx->block);
        ctx->block_len = 0U;
    }

    while (ctx->block_len < 56U) ctx->block[ctx->block_len++] = 0U;
    user_tls_store_be64(ctx->block + 56U, bit_len);
    user_tls_sha256_transform(ctx, ctx->block);

    for (uint32_t i = 0; i < 8U; i++) {
        user_tls_store_be32(out_digest + i * 4U, ctx->state[i]);
    }
}

void USER_CODE user_tls_sha256(const void* data, uint32_t len, uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]) {
    user_tls_sha256_ctx_t ctx;

    user_tls_sha256_init(&ctx);
    user_tls_sha256_update(&ctx, data, len);
    user_tls_sha256_final(&ctx, out_digest);
}

void USER_CODE user_tls_aes128_encrypt_block(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                             const uint8_t input[USER_TLS_AES_BLOCK_SIZE],
                                             uint8_t output[USER_TLS_AES_BLOCK_SIZE]) {
    uint8_t state[USER_TLS_AES_BLOCK_SIZE];
    uint8_t round_keys[176];

    if (!key || !input || !output) return;

    memcpy(state, input, sizeof(state));
    user_tls_aes_key_expand(key, round_keys);
    user_tls_aes_add_round_key(state, round_keys);
    for (uint32_t round = 1U; round < 10U; round++) {
        user_tls_aes_sub_bytes(state);
        user_tls_aes_shift_rows(state);
        user_tls_aes_mix_columns(state);
        user_tls_aes_add_round_key(state, round_keys + round * USER_TLS_AES_BLOCK_SIZE);
    }
    user_tls_aes_sub_bytes(state);
    user_tls_aes_shift_rows(state);
    user_tls_aes_add_round_key(state, round_keys + 10U * USER_TLS_AES_BLOCK_SIZE);
    memcpy(output, state, sizeof(state));
}

int USER_CODE user_tls_aes128_gcm_encrypt(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                          const uint8_t iv[USER_TLS_GCM_IV_SIZE],
                                          const void* aad, uint32_t aad_len,
                                          const void* plaintext, uint32_t plaintext_len,
                                          uint8_t* out_ciphertext,
                                          uint8_t out_tag[USER_TLS_GCM_TAG_SIZE]) {
    const uint8_t* input = (const uint8_t*)plaintext;
    uint8_t counter[USER_TLS_AES_BLOCK_SIZE];
    uint8_t stream[USER_TLS_AES_BLOCK_SIZE];
    uint32_t offset = 0U;

    if (!key || !iv || !out_tag) return -1;
    if (!aad && aad_len != 0U) return -1;
    if (!input && plaintext_len != 0U) return -1;
    if (!out_ciphertext && plaintext_len != 0U) return -1;

    memcpy(counter, iv, USER_TLS_GCM_IV_SIZE);
    counter[12] = 0x00U;
    counter[13] = 0x00U;
    counter[14] = 0x00U;
    counter[15] = 0x01U;
    user_tls_inc32(counter);

    while (offset < plaintext_len) {
        uint32_t block_len = plaintext_len - offset;
        if (block_len > USER_TLS_AES_BLOCK_SIZE) block_len = USER_TLS_AES_BLOCK_SIZE;
        user_tls_aes128_encrypt_block(key, counter, stream);
        for (uint32_t i = 0; i < block_len; i++) out_ciphertext[offset + i] = (uint8_t)(input[offset + i] ^ stream[i]);
        offset += block_len;
        user_tls_inc32(counter);
    }

    user_tls_gcm_compute_tag(key, iv, aad, aad_len, out_ciphertext, plaintext_len, out_tag);
    return 0;
}

int USER_CODE user_tls_aes128_gcm_decrypt(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                          const uint8_t iv[USER_TLS_GCM_IV_SIZE],
                                          const void* aad, uint32_t aad_len,
                                          const void* ciphertext, uint32_t ciphertext_len,
                                          const uint8_t tag[USER_TLS_GCM_TAG_SIZE],
                                          uint8_t* out_plaintext) {
    const uint8_t* input = (const uint8_t*)ciphertext;
    uint8_t expected_tag[USER_TLS_GCM_TAG_SIZE];
    uint8_t counter[USER_TLS_AES_BLOCK_SIZE];
    uint8_t stream[USER_TLS_AES_BLOCK_SIZE];
    uint32_t offset = 0U;

    if (!key || !iv || !tag) return -1;
    if (!aad && aad_len != 0U) return -1;
    if (!input && ciphertext_len != 0U) return -1;
    if (!out_plaintext && ciphertext_len != 0U) return -1;

    user_tls_gcm_compute_tag(key, iv, aad, aad_len, input, ciphertext_len, expected_tag);
    if (!user_memeq_consttime(expected_tag, tag, USER_TLS_GCM_TAG_SIZE)) return -1;

    memcpy(counter, iv, USER_TLS_GCM_IV_SIZE);
    counter[12] = 0x00U;
    counter[13] = 0x00U;
    counter[14] = 0x00U;
    counter[15] = 0x01U;
    user_tls_inc32(counter);

    while (offset < ciphertext_len) {
        uint32_t block_len = ciphertext_len - offset;
        if (block_len > USER_TLS_AES_BLOCK_SIZE) block_len = USER_TLS_AES_BLOCK_SIZE;
        user_tls_aes128_encrypt_block(key, counter, stream);
        for (uint32_t i = 0; i < block_len; i++) out_plaintext[offset + i] = (uint8_t)(input[offset + i] ^ stream[i]);
        offset += block_len;
        user_tls_inc32(counter);
    }
    return 0;
}

void USER_CODE user_tls_x25519_clamp_scalar(uint8_t scalar[USER_TLS_X25519_KEY_SIZE]) {
    if (!scalar) return;
    scalar[0] &= 248U;
    scalar[31] &= 127U;
    scalar[31] |= 64U;
}

int USER_CODE user_tls_x25519(uint8_t out_shared[USER_TLS_X25519_KEY_SIZE],
                              const uint8_t scalar[USER_TLS_X25519_KEY_SIZE],
                              const uint8_t u_coordinate[USER_TLS_X25519_KEY_SIZE]) {
    uint8_t e[USER_TLS_X25519_KEY_SIZE];
    user_tls_x25519_fe x1;
    user_tls_x25519_fe x2;
    user_tls_x25519_fe z2;
    user_tls_x25519_fe x3;
    user_tls_x25519_fe z3;
    user_tls_x25519_fe a;
    user_tls_x25519_fe b;
    user_tls_x25519_fe c;
    user_tls_x25519_fe d;
    user_tls_x25519_fe e_fe;
    user_tls_x25519_fe aa;
    user_tls_x25519_fe bb;
    user_tls_x25519_fe da;
    user_tls_x25519_fe cb;
    uint8_t swap = 0U;

    if (!out_shared || !scalar || !u_coordinate) return -1;

    memcpy(e, scalar, sizeof(e));
    user_tls_x25519_clamp_scalar(e);
    user_tls_x25519_fe_unpack(x1, u_coordinate);
    user_tls_x25519_fe_set_small(x2, 1);
    user_tls_x25519_fe_set_small(z2, 0);
    user_tls_x25519_fe_copy(x3, x1);
    user_tls_x25519_fe_set_small(z3, 1);

    /* Montgomery ladder over Curve25519 with RFC 7748 clamped scalar bytes. */
    for (int32_t pos = 254; pos >= 0; pos--) {
        uint8_t bit = (uint8_t)((e[pos >> 3] >> (pos & 7)) & 1U);

        swap ^= bit;
        user_tls_x25519_fe_swap(x2, x3, swap);
        user_tls_x25519_fe_swap(z2, z3, swap);
        swap = bit;

        user_tls_x25519_fe_add(a, x2, z2);
        user_tls_x25519_fe_sub(b, x2, z2);
        user_tls_x25519_fe_sq(aa, a);
        user_tls_x25519_fe_sq(bb, b);
        user_tls_x25519_fe_sub(e_fe, aa, bb);
        user_tls_x25519_fe_add(c, x3, z3);
        user_tls_x25519_fe_sub(d, x3, z3);
        user_tls_x25519_fe_mul(da, d, a);
        user_tls_x25519_fe_mul(cb, c, b);
        user_tls_x25519_fe_add(a, da, cb);
        user_tls_x25519_fe_sub(b, da, cb);
        user_tls_x25519_fe_sq(x3, a);
        user_tls_x25519_fe_sq(a, b);
        user_tls_x25519_fe_mul(z3, a, x1);
        user_tls_x25519_fe_mul(x2, aa, bb);
        user_tls_x25519_fe_mul(a, e_fe, user_tls_x25519_a24);
        user_tls_x25519_fe_add(a, a, aa);
        user_tls_x25519_fe_mul(z2, e_fe, a);
    }

    user_tls_x25519_fe_swap(x2, x3, swap);
    user_tls_x25519_fe_swap(z2, z3, swap);
    user_tls_x25519_fe_inv(z2, z2);
    user_tls_x25519_fe_mul(x2, x2, z2);
    user_tls_x25519_fe_pack(out_shared, x2);
    return 0;
}

int USER_CODE user_tls_x25519_public_key(uint8_t out_public[USER_TLS_X25519_KEY_SIZE],
                                         const uint8_t private_scalar[USER_TLS_X25519_KEY_SIZE]) {
    return user_tls_x25519(out_public, private_scalar, user_tls_x25519_basepoint);
}

static USER_CODE int user_tls_hmac_sha256_init(user_tls_hmac_sha256_ctx_t* ctx,
                                               const void* key, uint32_t key_len) {
    uint8_t key_block[USER_TLS_SHA256_BLOCK_SIZE];
    uint8_t opad_block[USER_TLS_SHA256_BLOCK_SIZE];
    uint8_t hashed_key[USER_TLS_SHA256_DIGEST_SIZE];
    const uint8_t* key_bytes = (const uint8_t*)key;

    if (!ctx) return -1;
    if (!key_bytes && key_len != 0U) return -1;

    memset(key_block, 0, sizeof(key_block));
    if (key_len > USER_TLS_SHA256_BLOCK_SIZE) {
        user_tls_sha256(key_bytes, key_len, hashed_key);
        memcpy(key_block, hashed_key, sizeof(hashed_key));
    } else if (key_len != 0U) {
        memcpy(key_block, key_bytes, key_len);
    }

    for (uint32_t i = 0; i < USER_TLS_SHA256_BLOCK_SIZE; i++) {
        opad_block[i] = (uint8_t)(key_block[i] ^ 0x5cU);
        key_block[i] = (uint8_t)(key_block[i] ^ 0x36U);
    }

    user_tls_sha256_init(&ctx->inner);
    user_tls_sha256_update(&ctx->inner, key_block, sizeof(key_block));
    user_tls_sha256_init(&ctx->outer);
    user_tls_sha256_update(&ctx->outer, opad_block, sizeof(opad_block));
    return 0;
}

static USER_CODE void user_tls_hmac_sha256_update(user_tls_hmac_sha256_ctx_t* ctx,
                                                  const void* data, uint32_t data_len) {
    if (!ctx || (!data && data_len != 0U)) return;
    user_tls_sha256_update(&ctx->inner, data, data_len);
}

static USER_CODE void user_tls_hmac_sha256_final(user_tls_hmac_sha256_ctx_t* ctx,
                                                 uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]) {
    uint8_t inner_digest[USER_TLS_SHA256_DIGEST_SIZE];

    if (!ctx || !out_digest) return;
    user_tls_sha256_final(&ctx->inner, inner_digest);
    user_tls_sha256_update(&ctx->outer, inner_digest, sizeof(inner_digest));
    user_tls_sha256_final(&ctx->outer, out_digest);
}

int USER_CODE user_tls_hmac_sha256(const void* key, uint32_t key_len,
                                   const void* data, uint32_t data_len,
                                   uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]) {
    user_tls_hmac_sha256_ctx_t ctx;

    if (!out_digest) return -1;
    if (user_tls_hmac_sha256_init(&ctx, key, key_len) != 0) return -1;
    user_tls_hmac_sha256_update(&ctx, data, data_len);
    user_tls_hmac_sha256_final(&ctx, out_digest);
    return 0;
}

int USER_CODE user_tls_hkdf_extract(const void* salt, uint32_t salt_len,
                                    const void* ikm, uint32_t ikm_len,
                                    uint8_t out_prk[USER_TLS_SHA256_DIGEST_SIZE]) {
    static const uint8_t zero_salt[USER_TLS_SHA256_DIGEST_SIZE] = { 0 };

    if (!out_prk) return -1;
    if (!ikm && ikm_len != 0U) return -1;
    if (!salt || salt_len == 0U) {
        return user_tls_hmac_sha256(zero_salt, sizeof(zero_salt), ikm, ikm_len, out_prk);
    }
    return user_tls_hmac_sha256(salt, salt_len, ikm, ikm_len, out_prk);
}

int USER_CODE user_tls_hkdf_expand(const uint8_t* prk, uint32_t prk_len,
                                   const void* info, uint32_t info_len,
                                   uint8_t* out_key, uint32_t out_len) {
    user_tls_hmac_sha256_ctx_t ctx;
    uint8_t block[USER_TLS_SHA256_DIGEST_SIZE];
    uint32_t produced = 0U;
    uint32_t block_len = 0U;
    uint8_t counter = 1U;

    if (!prk || prk_len == 0U) return -1;
    if (!info && info_len != 0U) return -1;
    if (!out_key && out_len != 0U) return -1;
    if (out_len > 255U * USER_TLS_SHA256_DIGEST_SIZE) return -1;

    while (produced < out_len) {
        uint32_t copy_len;

        if (user_tls_hmac_sha256_init(&ctx, prk, prk_len) != 0) return -1;
        if (block_len != 0U) user_tls_hmac_sha256_update(&ctx, block, block_len);
        if (info_len != 0U) user_tls_hmac_sha256_update(&ctx, info, info_len);
        user_tls_hmac_sha256_update(&ctx, &counter, 1U);
        user_tls_hmac_sha256_final(&ctx, block);
        block_len = sizeof(block);

        copy_len = out_len - produced;
        if (copy_len > sizeof(block)) copy_len = sizeof(block);
        memcpy(out_key + produced, block, copy_len);
        produced += copy_len;
        counter++;
        if (counter == 0U && produced < out_len) return -1;
    }
    return 0;
}

int USER_CODE user_tls_hkdf_expand_label(const uint8_t* secret, uint32_t secret_len,
                                         const char* label,
                                         const void* context, uint8_t context_len,
                                         uint8_t* out_key, uint32_t out_len) {
    uint8_t info[514];
    static const char prefix[] = "tls13 ";
    uint32_t label_len;
    uint32_t full_label_len;
    uint32_t info_len = 0U;

    if (!label) return -1;
    if (!context && context_len != 0U) return -1;
    if (out_len > 0xFFFFU) return -1;

    label_len = (uint32_t)strlen(label);
    full_label_len = (uint32_t)(sizeof(prefix) - 1U) + label_len;
    if (full_label_len > 255U) return -1;

    /* TLS 1.3 HKDF-Expand-Label serializes length, "tls13 "+label, then context. */
    info[info_len++] = (uint8_t)(out_len >> 8);
    info[info_len++] = (uint8_t)out_len;
    info[info_len++] = (uint8_t)full_label_len;
    memcpy(info + info_len, prefix, sizeof(prefix) - 1U);
    info_len += (uint32_t)(sizeof(prefix) - 1U);
    memcpy(info + info_len, label, label_len);
    info_len += label_len;
    info[info_len++] = context_len;
    if (context_len != 0U) {
        memcpy(info + info_len, context, context_len);
        info_len += context_len;
    }

    return user_tls_hkdf_expand(secret, secret_len, info, info_len, out_key, out_len);
}

int USER_CODE user_tls_mgf1_sha256(const void* seed, uint32_t seed_len, uint8_t* out_mask, uint32_t mask_len) {
    const uint8_t* seed_bytes = (const uint8_t*)seed;
    uint8_t digest[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t counter[4];
    uint8_t block[USER_TLS_BIGINT_MAX_BYTES + 4U];
    uint32_t generated = 0U;

    if (!out_mask && mask_len != 0U) return -1;
    if (!seed_bytes && seed_len != 0U) return -1;
    if (seed_len > USER_TLS_BIGINT_MAX_BYTES) return -1;

    while (generated < mask_len) {
        uint32_t copy_len = mask_len - generated;
        uint32_t count = generated / USER_TLS_SHA256_DIGEST_SIZE;

        counter[0] = (uint8_t)(count >> 24);
        counter[1] = (uint8_t)(count >> 16);
        counter[2] = (uint8_t)(count >> 8);
        counter[3] = (uint8_t)count;
        if (seed_len != 0U) memcpy(block, seed_bytes, seed_len);
        memcpy(block + seed_len, counter, sizeof(counter));
        user_tls_sha256(block, seed_len + sizeof(counter), digest);
        if (copy_len > USER_TLS_SHA256_DIGEST_SIZE) copy_len = USER_TLS_SHA256_DIGEST_SIZE;
        memcpy(out_mask + generated, digest, copy_len);
        generated += copy_len;
    }
    return 0;
}

static USER_CODE uint32_t user_tls_count_leading_zero_bits_u8(uint8_t value) {
    uint32_t count = 0U;

    while (((value & 0x80U) == 0U) && count < 8U) {
        value <<= 1;
        count++;
    }
    return count;
}

static USER_CODE uint32_t user_tls_modulus_bitlen(const uint8_t* modulus, uint32_t modulus_len) {
    uint32_t index = 0U;

    if (!modulus) return 0U;
    while (index < modulus_len && modulus[index] == 0U) index++;
    if (index == modulus_len) return 0U;
    return (modulus_len - index) * 8U - user_tls_count_leading_zero_bits_u8(modulus[index]);
}

int USER_CODE user_tls_rsa_pss_sha256_verify(const uint8_t* modulus, uint32_t modulus_len,
                                             uint32_t exponent,
                                             const uint8_t* signature, uint32_t signature_len,
                                             const void* message, uint32_t message_len) {
    user_tls_bigint_t n;
    user_tls_bigint_t s;
    user_tls_bigint_t m;
    uint8_t encoded[USER_TLS_BIGINT_MAX_BYTES];
    uint8_t em[USER_TLS_BIGINT_MAX_BYTES];
    uint8_t db_mask[USER_TLS_BIGINT_MAX_BYTES];
    uint8_t db[USER_TLS_BIGINT_MAX_BYTES];
    uint8_t m_hash[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t h_prime[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t m_prime[8U + USER_TLS_SHA256_DIGEST_SIZE + USER_TLS_SHA256_DIGEST_SIZE];
    uint32_t mod_bits;
    uint32_t em_bits;
    uint32_t em_len;
    uint32_t top_bits;
    uint32_t db_len;
    uint32_t salt_len = USER_TLS_SHA256_DIGEST_SIZE;
    const uint8_t* h;

    if (!modulus || !signature || !message) return -1;
    if (modulus_len == 0U || modulus_len > USER_TLS_BIGINT_MAX_BYTES) return -1;
    if (signature_len != modulus_len) return -1;
    if (exponent == 0U) return -1;

    mod_bits = user_tls_modulus_bitlen(modulus, modulus_len);
    if (mod_bits == 0U) return -1;
    em_bits = mod_bits - 1U;
    em_len = (em_bits + 7U) >> 3;
    if (em_len < USER_TLS_SHA256_DIGEST_SIZE + salt_len + 2U) return -1;

    if (user_tls_bigint_from_be_bytes(&n, modulus, modulus_len) != 0 ||
        user_tls_bigint_from_be_bytes(&s, signature, signature_len) != 0) return -1;
    if (user_tls_bigint_compare(&s, &n) >= 0) return -1;
    if (user_tls_bigint_modexp_u32(&m, &s, exponent, &n) != 0) return -1;

    user_tls_bigint_to_be_bytes(&m, encoded, modulus_len);
    if (modulus_len < em_len) return -1;
    if (modulus_len > em_len) {
        uint32_t prefix_len = modulus_len - em_len;
        for (uint32_t i = 0; i < prefix_len; i++) {
            if (encoded[i] != 0U) return -1;
        }
        memcpy(em, encoded + prefix_len, em_len);
    } else {
        memcpy(em, encoded, em_len);
    }

    if (em[em_len - 1U] != 0xbcU) return -1;
    db_len = em_len - USER_TLS_SHA256_DIGEST_SIZE - 1U;
    h = em + db_len;
    top_bits = 8U * em_len - em_bits;
    if (top_bits != 0U && (em[0] & (uint8_t)(0xFFU << (8U - top_bits))) != 0U) return -1;
    if (user_tls_mgf1_sha256(h, USER_TLS_SHA256_DIGEST_SIZE, db_mask, db_len) != 0) return -1;
    for (uint32_t i = 0; i < db_len; i++) db[i] = (uint8_t)(em[i] ^ db_mask[i]);
    if (top_bits != 0U) db[0] &= (uint8_t)(0xFFU >> top_bits);

    if (db_len < salt_len + 1U) return -1;
    for (uint32_t i = 0; i < db_len - salt_len - 1U; i++) {
        if (db[i] != 0U) return -1;
    }
    if (db[db_len - salt_len - 1U] != 0x01U) return -1;

    user_tls_sha256(message, message_len, m_hash);
    memset(m_prime, 0, 8U);
    memcpy(m_prime + 8U, m_hash, USER_TLS_SHA256_DIGEST_SIZE);
    memcpy(m_prime + 8U + USER_TLS_SHA256_DIGEST_SIZE, db + db_len - salt_len, salt_len);
    user_tls_sha256(m_prime, sizeof(m_prime), h_prime);
    return user_memeq_consttime(h, h_prime, USER_TLS_SHA256_DIGEST_SIZE) ? 0 : -1;
}

void USER_CODE user_tls_transcript_init(user_tls_transcript_hash_t* ctx) {
    user_tls_sha256_init(ctx);
}

void USER_CODE user_tls_transcript_update(user_tls_transcript_hash_t* ctx, const void* data, uint32_t len) {
    user_tls_sha256_update(ctx, data, len);
}

void USER_CODE user_tls_transcript_final(user_tls_transcript_hash_t* ctx,
                                         uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]) {
    user_tls_sha256_final(ctx, out_digest);
}

static USER_CODE int user_tls_selftest_expect_digest(const uint8_t* actual,
                                                     const uint8_t* expected, uint32_t len) {
    return actual && expected && user_memeq_consttime(actual, expected, len) ? 0 : -1;
}

int USER_CODE user_tls_crypto_selftest_hash_kdf(char* detail, uint32_t detail_len) {
    uint8_t digest[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t okm[42];
    user_tls_transcript_hash_t transcript;

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    user_tls_sha256(0, 0U, digest);
    if (user_tls_selftest_expect_digest(digest, user_tls_sha256_empty_expected, sizeof(digest)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_sha256_empty_fail);
        return -1;
    }

    user_tls_sha256("abc", 3U, digest);
    if (user_tls_selftest_expect_digest(digest, user_tls_sha256_abc_expected, sizeof(digest)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_sha256_abc_fail);
        return -1;
    }

    if (user_tls_hmac_sha256(user_tls_hmac_tc1_key, sizeof(user_tls_hmac_tc1_key),
                             user_tls_hmac_tc1_data, sizeof(user_tls_hmac_tc1_data),
                             digest) != 0 ||
        user_tls_selftest_expect_digest(digest, user_tls_hmac_tc1_expected, sizeof(digest)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_hmac_tc1_fail);
        return -1;
    }

    if (user_tls_hmac_sha256(user_tls_hmac_tc2_key, sizeof(user_tls_hmac_tc2_key),
                             user_tls_hmac_tc2_data, sizeof(user_tls_hmac_tc2_data),
                             digest) != 0 ||
        user_tls_selftest_expect_digest(digest, user_tls_hmac_tc2_expected, sizeof(digest)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_hmac_tc2_fail);
        return -1;
    }

    if (user_tls_hkdf_extract(user_tls_hkdf_tc1_salt, sizeof(user_tls_hkdf_tc1_salt),
                              user_tls_hkdf_tc1_ikm, sizeof(user_tls_hkdf_tc1_ikm),
                              digest) != 0 ||
        user_tls_selftest_expect_digest(digest, user_tls_hkdf_tc1_prk, sizeof(digest)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_hkdf_extract_fail);
        return -1;
    }

    if (user_tls_hkdf_expand(user_tls_hkdf_tc1_prk, sizeof(user_tls_hkdf_tc1_prk),
                             user_tls_hkdf_tc1_info, sizeof(user_tls_hkdf_tc1_info),
                             okm, sizeof(okm)) != 0 ||
        user_tls_selftest_expect_digest(okm, user_tls_hkdf_tc1_okm, sizeof(okm)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_hkdf_expand_fail);
        return -1;
    }

    if (user_tls_hkdf_expand_label(user_tls_expand_label_secret, sizeof(user_tls_expand_label_secret),
                                   user_tls_expand_label_name,
                                   user_tls_expand_label_context, sizeof(user_tls_expand_label_context),
                                   digest, 16U) != 0 ||
        user_tls_selftest_expect_digest(digest, user_tls_expand_label_expected, 16U) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_label_fail);
        return -1;
    }

    user_tls_transcript_init(&transcript);
    user_tls_transcript_update(&transcript, "abc", 3U);
    user_tls_transcript_update(&transcript, "def", 3U);
    user_tls_transcript_final(&transcript, digest);
    if (user_tls_selftest_expect_digest(digest, user_tls_sha256_abcdef_expected, sizeof(digest)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_transcript_fail);
        return -1;
    }

    if (user_tls_hkdf_expand(0, 0U, 0, 0U, digest, sizeof(digest)) == 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_invalid);
        return -1;
    }

    user_tls_copy_detail(detail, detail_len, user_tls_selftest_ok);
    return 0;
}

int USER_CODE user_tls_crypto_selftest_aes_gcm(char* detail, uint32_t detail_len) {
    uint8_t block[USER_TLS_AES_BLOCK_SIZE];
    uint8_t ciphertext[60];
    uint8_t plaintext[60];
    uint8_t tag[USER_TLS_GCM_TAG_SIZE];

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    user_tls_aes128_encrypt_block(user_tls_aes_block_key, user_tls_aes_block_plain, block);
    if (user_tls_selftest_expect_digest(block, user_tls_aes_block_expected, sizeof(block)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_aes_block_fail);
        return -1;
    }

    if (user_tls_aes128_gcm_encrypt(user_tls_gcm_zero_key, user_tls_gcm_zero_iv,
                                    0, 0U, 0, 0U, 0, tag) != 0 ||
        user_tls_selftest_expect_digest(tag, user_tls_gcm_empty_tag, sizeof(tag)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_gcm_empty_fail);
        return -1;
    }

    if (user_tls_aes128_gcm_encrypt(user_tls_gcm_zero_key, user_tls_gcm_zero_iv,
                                    0, 0U,
                                    user_tls_gcm_zero_plain, sizeof(user_tls_gcm_zero_plain),
                                    ciphertext, tag) != 0 ||
        user_tls_selftest_expect_digest(ciphertext, user_tls_gcm_zero_cipher, sizeof(user_tls_gcm_zero_cipher)) != 0 ||
        user_tls_selftest_expect_digest(tag, user_tls_gcm_zero_tag, sizeof(tag)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_gcm_zero_fail);
        return -1;
    }

    if (user_tls_aes128_gcm_decrypt(user_tls_gcm_zero_key, user_tls_gcm_zero_iv,
                                    0, 0U,
                                    user_tls_gcm_zero_cipher, sizeof(user_tls_gcm_zero_cipher),
                                    user_tls_gcm_zero_tag, plaintext) != 0 ||
        user_tls_selftest_expect_digest(plaintext, user_tls_gcm_zero_plain, sizeof(user_tls_gcm_zero_plain)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_gcm_decrypt_fail);
        return -1;
    }

    if (user_tls_aes128_gcm_encrypt(user_tls_gcm_long_key, user_tls_gcm_long_iv,
                                    user_tls_gcm_long_aad, sizeof(user_tls_gcm_long_aad),
                                    user_tls_gcm_long_plain, sizeof(user_tls_gcm_long_plain),
                                    ciphertext, tag) != 0 ||
        user_tls_selftest_expect_digest(ciphertext, user_tls_gcm_long_cipher, sizeof(user_tls_gcm_long_cipher)) != 0 ||
        user_tls_selftest_expect_digest(tag, user_tls_gcm_long_tag, sizeof(tag)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_gcm_long_fail);
        return -1;
    }

    if (user_tls_aes128_gcm_decrypt(user_tls_gcm_long_key, user_tls_gcm_long_iv,
                                    user_tls_gcm_long_aad, sizeof(user_tls_gcm_long_aad),
                                    user_tls_gcm_long_cipher, sizeof(user_tls_gcm_long_cipher),
                                    user_tls_gcm_long_tag, plaintext) != 0 ||
        user_tls_selftest_expect_digest(plaintext, user_tls_gcm_long_plain, sizeof(user_tls_gcm_long_plain)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_gcm_decrypt_fail);
        return -1;
    }

    memcpy(tag, user_tls_gcm_long_tag, sizeof(tag));
    tag[0] ^= 0x01U;
    if (user_tls_aes128_gcm_decrypt(user_tls_gcm_long_key, user_tls_gcm_long_iv,
                                    user_tls_gcm_long_aad, sizeof(user_tls_gcm_long_aad),
                                    user_tls_gcm_long_cipher, sizeof(user_tls_gcm_long_cipher),
                                    tag, plaintext) == 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_gcm_bad_tag_fail);
        return -1;
    }

    user_tls_copy_detail(detail, detail_len, user_tls_selftest_aes_ok);
    return 0;
}

int USER_CODE user_tls_crypto_selftest_x25519(char* detail, uint32_t detail_len) {
    uint8_t scalar[USER_TLS_X25519_KEY_SIZE];
    uint8_t out[USER_TLS_X25519_KEY_SIZE];
    uint8_t alice_pub[USER_TLS_X25519_KEY_SIZE];
    uint8_t bob_pub[USER_TLS_X25519_KEY_SIZE];
    uint8_t shared0[USER_TLS_X25519_KEY_SIZE];
    uint8_t shared1[USER_TLS_X25519_KEY_SIZE];

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    memset(scalar, 0xff, sizeof(scalar));
    user_tls_x25519_clamp_scalar(scalar);
    if (scalar[0] != 0xf8U || scalar[31] != 0x7fU) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_clamp_fail);
        return -1;
    }

    if (user_tls_x25519(out, user_tls_x25519_scalar_tc1, user_tls_x25519_u_tc1) != 0 ||
        user_tls_selftest_expect_digest(out, user_tls_x25519_out_tc1, sizeof(out)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_tc1_fail);
        return -1;
    }

    if (user_tls_x25519(out, user_tls_x25519_scalar_tc2, user_tls_x25519_u_tc2) != 0 ||
        user_tls_selftest_expect_digest(out, user_tls_x25519_out_tc2, sizeof(out)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_tc2_fail);
        return -1;
    }

    if (user_tls_x25519(out, user_tls_x25519_basepoint, user_tls_x25519_basepoint) != 0 ||
        user_tls_selftest_expect_digest(out, user_tls_x25519_one_iter, sizeof(out)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_iter_fail);
        return -1;
    }

    if (user_tls_x25519_public_key(alice_pub, user_tls_x25519_alice_priv) != 0 ||
        user_tls_selftest_expect_digest(alice_pub, user_tls_x25519_alice_pub, sizeof(alice_pub)) != 0 ||
        user_tls_x25519_public_key(bob_pub, user_tls_x25519_bob_priv) != 0 ||
        user_tls_selftest_expect_digest(bob_pub, user_tls_x25519_bob_pub, sizeof(bob_pub)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_pub_fail);
        return -1;
    }

    if (user_tls_x25519(shared0, user_tls_x25519_alice_priv, user_tls_x25519_bob_pub) != 0 ||
        user_tls_x25519(shared1, user_tls_x25519_bob_priv, user_tls_x25519_alice_pub) != 0 ||
        user_tls_selftest_expect_digest(shared0, user_tls_x25519_shared, sizeof(shared0)) != 0 ||
        user_tls_selftest_expect_digest(shared1, user_tls_x25519_shared, sizeof(shared1)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_shared_fail);
        return -1;
    }

    user_tls_copy_detail(detail, detail_len, user_tls_selftest_x25519_ok);
    return 0;
}

int USER_CODE user_tls_crypto_selftest_rsa_pss(char* detail, uint32_t detail_len) {
    uint8_t tampered_sig[sizeof(user_tls_rsapss_signature)];
    char bigint_detail[64];

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    if (user_tls_bigint_selftest(bigint_detail, sizeof(bigint_detail)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_rsapss_bigint_fail);
        return -1;
    }

    if (user_tls_rsa_pss_sha256_verify(user_tls_rsapss_modulus, sizeof(user_tls_rsapss_modulus),
                                       65537U,
                                       user_tls_rsapss_signature, sizeof(user_tls_rsapss_signature),
                                       user_tls_rsapss_message, sizeof(user_tls_rsapss_message)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_rsapss_verify_fail);
        return -1;
    }

    memcpy(tampered_sig, user_tls_rsapss_signature, sizeof(tampered_sig));
    tampered_sig[0] ^= 0x01U;
    if (user_tls_rsa_pss_sha256_verify(user_tls_rsapss_modulus, sizeof(user_tls_rsapss_modulus),
                                       65537U,
                                       tampered_sig, sizeof(tampered_sig),
                                       user_tls_rsapss_message, sizeof(user_tls_rsapss_message)) == 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_rsapss_reject_fail);
        return -1;
    }

    user_tls_copy_detail(detail, detail_len, user_tls_selftest_rsapss_ok);
    return 0;
}
