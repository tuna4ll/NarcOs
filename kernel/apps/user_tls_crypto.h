#ifndef USER_TLS_CRYPTO_H
#define USER_TLS_CRYPTO_H

#include <stdint.h>

#define USER_TLS_SHA256_DIGEST_SIZE 32U
#define USER_TLS_SHA256_BLOCK_SIZE 64U
#define USER_TLS_AES128_KEY_SIZE 16U
#define USER_TLS_AES_BLOCK_SIZE 16U
#define USER_TLS_GCM_IV_SIZE 12U
#define USER_TLS_GCM_TAG_SIZE 16U
#define USER_TLS_X25519_KEY_SIZE 32U

typedef struct {
    uint32_t state[8];
    uint64_t total_len;
    uint32_t block_len;
    uint8_t block[USER_TLS_SHA256_BLOCK_SIZE];
} user_tls_sha256_ctx_t;

typedef user_tls_sha256_ctx_t user_tls_transcript_hash_t;

void user_tls_sha256_init(user_tls_sha256_ctx_t* ctx);
void user_tls_sha256_update(user_tls_sha256_ctx_t* ctx, const void* data, uint32_t len);
void user_tls_sha256_final(user_tls_sha256_ctx_t* ctx, uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]);
void user_tls_sha256(const void* data, uint32_t len, uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]);

int user_tls_hmac_sha256(const void* key, uint32_t key_len,
                         const void* data, uint32_t data_len,
                         uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]);
int user_tls_hkdf_extract(const void* salt, uint32_t salt_len,
                          const void* ikm, uint32_t ikm_len,
                          uint8_t out_prk[USER_TLS_SHA256_DIGEST_SIZE]);
int user_tls_hkdf_expand(const uint8_t* prk, uint32_t prk_len,
                         const void* info, uint32_t info_len,
                         uint8_t* out_key, uint32_t out_len);
int user_tls_hkdf_expand_label(const uint8_t* secret, uint32_t secret_len,
                               const char* label,
                               const void* context, uint8_t context_len,
                               uint8_t* out_key, uint32_t out_len);

void user_tls_aes128_encrypt_block(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                   const uint8_t input[USER_TLS_AES_BLOCK_SIZE],
                                   uint8_t output[USER_TLS_AES_BLOCK_SIZE]);
int user_tls_aes128_gcm_encrypt(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                const uint8_t iv[USER_TLS_GCM_IV_SIZE],
                                const void* aad, uint32_t aad_len,
                                const void* plaintext, uint32_t plaintext_len,
                                uint8_t* out_ciphertext,
                                uint8_t out_tag[USER_TLS_GCM_TAG_SIZE]);
int user_tls_aes128_gcm_decrypt(const uint8_t key[USER_TLS_AES128_KEY_SIZE],
                                const uint8_t iv[USER_TLS_GCM_IV_SIZE],
                                const void* aad, uint32_t aad_len,
                                const void* ciphertext, uint32_t ciphertext_len,
                                const uint8_t tag[USER_TLS_GCM_TAG_SIZE],
                                uint8_t* out_plaintext);
void user_tls_x25519_clamp_scalar(uint8_t scalar[USER_TLS_X25519_KEY_SIZE]);
int user_tls_x25519(uint8_t out_shared[USER_TLS_X25519_KEY_SIZE],
                    const uint8_t scalar[USER_TLS_X25519_KEY_SIZE],
                    const uint8_t u_coordinate[USER_TLS_X25519_KEY_SIZE]);
int user_tls_x25519_public_key(uint8_t out_public[USER_TLS_X25519_KEY_SIZE],
                               const uint8_t private_scalar[USER_TLS_X25519_KEY_SIZE]);
int user_tls_mgf1_sha256(const void* seed, uint32_t seed_len, uint8_t* out_mask, uint32_t mask_len);
int user_tls_rsa_pss_sha256_verify(const uint8_t* modulus, uint32_t modulus_len,
                                   uint32_t exponent,
                                   const uint8_t* signature, uint32_t signature_len,
                                   const void* message, uint32_t message_len);
int user_tls_ecdsa_p256_sha256_verify(const uint8_t public_x[32], const uint8_t public_y[32],
                                      const uint8_t* signature_der, uint32_t signature_len,
                                      const void* message, uint32_t message_len);

void user_tls_transcript_init(user_tls_transcript_hash_t* ctx);
void user_tls_transcript_update(user_tls_transcript_hash_t* ctx, const void* data, uint32_t len);
void user_tls_transcript_final(user_tls_transcript_hash_t* ctx,
                               uint8_t out_digest[USER_TLS_SHA256_DIGEST_SIZE]);

int user_tls_crypto_selftest_hash_kdf(char* detail, uint32_t detail_len);
int user_tls_crypto_selftest_aes_gcm(char* detail, uint32_t detail_len);
int user_tls_crypto_selftest_x25519(char* detail, uint32_t detail_len);
int user_tls_crypto_selftest_rsa_pss(char* detail, uint32_t detail_len);
int user_tls_crypto_selftest_ecdsa_p256(char* detail, uint32_t detail_len);

#endif
