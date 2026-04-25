#ifndef USER_TLS_X509_H
#define USER_TLS_X509_H

#include <stdint.h>
#include "user_tls_crypto.h"

#define USER_TLS_X509_MAX_DNS_NAMES 8U

typedef struct {
    const uint8_t* data;
    uint32_t len;
} user_tls_x509_span_t;

typedef enum {
    USER_TLS_X509_KEY_UNKNOWN = 0,
    USER_TLS_X509_KEY_RSA = 1,
    USER_TLS_X509_KEY_EC_P256 = 2
} user_tls_x509_key_type_t;

typedef struct {
    const uint8_t* der;
    uint32_t der_len;
    user_tls_x509_span_t tbs_certificate;
    user_tls_x509_span_t subject_public_key_info;
    user_tls_x509_key_type_t key_type;
    user_tls_x509_span_t rsa_modulus;
    uint32_t rsa_exponent;
    user_tls_x509_span_t ec_public_x;
    user_tls_x509_span_t ec_public_y;
    uint64_t not_before_unix;
    uint64_t not_after_unix;
    uint32_t dns_name_count;
    user_tls_x509_span_t dns_names[USER_TLS_X509_MAX_DNS_NAMES];
} user_tls_x509_cert_t;

int user_tls_x509_parse_leaf(user_tls_x509_cert_t* out_cert, const uint8_t* der, uint32_t der_len);
int user_tls_x509_spki_sha256(const user_tls_x509_cert_t* cert,
                              uint8_t out_hash[USER_TLS_SHA256_DIGEST_SIZE]);
int user_tls_x509_hostname_matches(const user_tls_x509_cert_t* cert, const char* hostname);
int user_tls_x509_is_valid_at(const user_tls_x509_cert_t* cert, uint64_t unix_time);
int user_tls_x509_selftest(char* detail, uint32_t detail_len);

#endif
