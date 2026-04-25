#include <stdint.h>
#include "user_tls.h"
#include "user_tls_crypto.h"
#include "user_tls_pins.h"
#include "user_tls_x509.h"
#include "user_abi.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

#define memset user_memset
#define memcpy user_memcpy
#define memcmp user_memcmp
#define strlen user_strlen

#define USER_TLS_SOCKET_PORT 443U
#define USER_TLS_RECORD_HEADER_SIZE 5U
#define USER_TLS_RECORD_PLAINTEXT_MAX 16384U
#define USER_TLS_RECORD_CIPHERTEXT_MAX (USER_TLS_RECORD_PLAINTEXT_MAX + 256U)
#define USER_TLS_HANDSHAKE_BUFFER_SIZE 32768U
#define USER_TLS_CERT_BUFFER_SIZE 8192U
#define USER_TLS_RECORD_READ_IDLE_TIMEOUT 300U
#define USER_TLS_RECORD_READ_TOTAL_TIMEOUT 2400U
#define USER_TLS_INTERNAL_SKIP 1

enum {
    USER_TLS_CONTENT_CHANGE_CIPHER_SPEC = 20U,
    USER_TLS_CONTENT_ALERT = 21U,
    USER_TLS_CONTENT_HANDSHAKE = 22U,
    USER_TLS_CONTENT_APPLICATION_DATA = 23U
};

enum {
    USER_TLS_HANDSHAKE_CLIENT_HELLO = 1U,
    USER_TLS_HANDSHAKE_SERVER_HELLO = 2U,
    USER_TLS_HANDSHAKE_NEW_SESSION_TICKET = 4U,
    USER_TLS_HANDSHAKE_ENCRYPTED_EXTENSIONS = 8U,
    USER_TLS_HANDSHAKE_CERTIFICATE = 11U,
    USER_TLS_HANDSHAKE_CERTIFICATE_VERIFY = 15U,
    USER_TLS_HANDSHAKE_FINISHED = 20U
};

enum {
    USER_TLS_EXT_SERVER_NAME = 0x0000U,
    USER_TLS_EXT_SUPPORTED_GROUPS = 0x000aU,
    USER_TLS_EXT_SIGNATURE_ALGORITHMS = 0x000dU,
    USER_TLS_EXT_SIGNATURE_ALGORITHMS_CERT = 0x0032U,
    USER_TLS_EXT_SUPPORTED_VERSIONS = 0x002bU,
    USER_TLS_EXT_PSK_KEY_EXCHANGE_MODES = 0x002dU,
    USER_TLS_EXT_KEY_SHARE = 0x0033U
};

enum {
    USER_TLS_GROUP_X25519 = 0x001dU,
    USER_TLS_CIPHER_TLS_AES_128_GCM_SHA256 = 0x1301U,
    USER_TLS_SIG_ECDSA_SECP256R1_SHA256 = 0x0403U,
    USER_TLS_SIG_RSA_PSS_RSAE_SHA256 = 0x0804U,
    USER_TLS_SIG_RSA_PSS_PSS_SHA256 = 0x0809U,
    USER_TLS_VERSION_TLS12 = 0x0303U,
    USER_TLS_VERSION_TLS13 = 0x0304U,
    USER_TLS_RECORD_VERSION_CLIENT_HELLO = 0x0301U
};

enum {
    USER_TLS_DEBUG_STAGE_IDLE = 0U,
    USER_TLS_DEBUG_STAGE_OPEN = 1U,
    USER_TLS_DEBUG_STAGE_HANDSHAKE = 2U,
    USER_TLS_DEBUG_STAGE_SERVER_HELLO = 3U,
    USER_TLS_DEBUG_STAGE_HANDSHAKE_KEYS = 4U,
    USER_TLS_DEBUG_STAGE_ENCRYPTED_EXTENSIONS = 5U,
    USER_TLS_DEBUG_STAGE_CERTIFICATE = 6U,
    USER_TLS_DEBUG_STAGE_CERTIFICATE_VERIFY = 7U,
    USER_TLS_DEBUG_STAGE_SERVER_FINISHED = 8U,
    USER_TLS_DEBUG_STAGE_CLIENT_FINISHED = 9U,
    USER_TLS_DEBUG_STAGE_APPDATA_SEND = 10U,
    USER_TLS_DEBUG_STAGE_APPDATA_RECV = 11U,
    USER_TLS_DEBUG_STAGE_RECORD_ENCRYPT = 12U,
    USER_TLS_DEBUG_STAGE_RECORD_DECRYPT = 13U,
    USER_TLS_DEBUG_STAGE_CLOSE = 14U
};

typedef struct {
    uint8_t handshake_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t master_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t client_handshake_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t server_handshake_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t client_finished_key[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t server_finished_key[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t client_handshake_key[USER_TLS_AES128_KEY_SIZE];
    uint8_t server_handshake_key[USER_TLS_AES128_KEY_SIZE];
    uint8_t client_handshake_iv[USER_TLS_GCM_IV_SIZE];
    uint8_t server_handshake_iv[USER_TLS_GCM_IV_SIZE];
    uint8_t client_application_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t server_application_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t client_application_key[USER_TLS_AES128_KEY_SIZE];
    uint8_t server_application_key[USER_TLS_AES128_KEY_SIZE];
    uint8_t client_application_iv[USER_TLS_GCM_IV_SIZE];
    uint8_t server_application_iv[USER_TLS_GCM_IV_SIZE];
} user_tls_key_schedule_t;

typedef struct {
    const uint8_t* data;
    uint32_t len;
    uint32_t off;
} user_tls_cursor_t;

typedef struct {
    int socket_handle;
    int last_error;
    uint32_t validated_unix_time;
    uint32_t peer_ip;
    char server_name[USER_TLS_MAX_HOSTNAME_LEN + 1U];
    uint8_t client_random[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t server_random[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t session_id[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t client_private[USER_TLS_X25519_KEY_SIZE];
    uint8_t client_public[USER_TLS_X25519_KEY_SIZE];
    uint8_t server_public[USER_TLS_X25519_KEY_SIZE];
    uint8_t last_alert_level;
    uint8_t last_alert_description;
    uint8_t read_has_keys;
    uint8_t write_has_keys;
    uint8_t read_use_application_keys;
    uint8_t write_use_application_keys;
    uint8_t handshake_complete;
    uint8_t peer_closed;
    uint8_t close_notify_sent;
    uint64_t handshake_read_seq;
    uint64_t handshake_write_seq;
    uint64_t application_read_seq;
    uint64_t application_write_seq;
    user_tls_key_schedule_t keys;
    user_tls_transcript_hash_t transcript;
    user_tls_x509_cert_t server_cert;
    uint8_t* record_buf;
    uint8_t* plain_buf;
    uint8_t* message_buf;
    uint32_t message_buf_len;
    uint32_t message_buf_off;
    uint8_t* cert_buf;
} user_tls_client_t;

static user_tls_client_t* user_tls_client_state = (user_tls_client_t*)0;
static user_tls_client_t user_tls_client_storage;
static uint8_t user_tls_record_storage[USER_TLS_RECORD_HEADER_SIZE + USER_TLS_RECORD_CIPHERTEXT_MAX];
static uint8_t user_tls_plain_storage[USER_TLS_RECORD_PLAINTEXT_MAX + 1U];
static uint8_t user_tls_message_storage[USER_TLS_HANDSHAKE_BUFFER_SIZE];
static uint8_t user_tls_cert_storage[USER_TLS_CERT_BUFFER_SIZE];
static uint32_t user_tls_debug_stage = USER_TLS_DEBUG_STAGE_IDLE;
static int user_tls_debug_status_code = 0;
static char user_tls_debug_detail_storage[64];

static const char user_tls_default_ntp_host_name[] USER_RODATA = "time.google.com";
static const char user_tls_name_framework[] USER_RODATA = "framework";
static const char user_tls_name_hash_kdf[] USER_RODATA = "hash_kdf";
static const char user_tls_name_aes_gcm[] USER_RODATA = "aes_gcm";
static const char user_tls_name_x25519[] USER_RODATA = "x25519";
static const char user_tls_name_rsa_pss[] USER_RODATA = "rsa_pss";
static const char user_tls_name_ecdsa_p256[] USER_RODATA = "ecdsa_p256";
static const char user_tls_name_x509[] USER_RODATA = "x509";
static const char user_tls_name_tls13[] USER_RODATA = "tls13";

static const char user_tls_detail_dispatch_ok[] USER_RODATA = "dispatch ok";

static const char user_tls_status_pass[] USER_RODATA = "PASS";
static const char user_tls_status_fail[] USER_RODATA = "FAIL";
static const char user_tls_status_pending[] USER_RODATA = "PENDING";
static const char user_tls_status_skip[] USER_RODATA = "SKIP";
static const char user_tls_status_unknown[] USER_RODATA = "UNKNOWN";

static const char user_tls_error_invalid[] USER_RODATA = "invalid request";
static const char user_tls_error_alloc[] USER_RODATA = "allocation failed";
static const char user_tls_error_protocol[] USER_RODATA = "tls protocol error";
static const char user_tls_error_crypto[] USER_RODATA = "tls crypto failure";
static const char user_tls_error_pin[] USER_RODATA = "tls pin check failed";
static const char user_tls_error_hostname[] USER_RODATA = "hostname mismatch";
static const char user_tls_error_certificate[] USER_RODATA = "certificate verify failed";
static const char user_tls_error_cert_time[] USER_RODATA = "certificate time invalid";
static const char user_tls_error_not_connected[] USER_RODATA = "tls not connected";
static const char user_tls_error_record_overflow[] USER_RODATA = "tls record overflow";
static const char user_tls_error_alert[] USER_RODATA = "tls alert";
static const char user_tls_error_unknown[] USER_RODATA = "unknown error";
static const char user_tls_error_ntp[] USER_RODATA = "ntp unavailable";
static const char user_tls_error_unsupported[] USER_RODATA = "unsupported";
static const char user_tls_error_network_not_ready[] USER_RODATA = "network not ready";
static const char user_tls_error_no_sockets[] USER_RODATA = "no sockets available";
static const char user_tls_error_dns_resolve[] USER_RODATA = "dns resolve failed";
static const char user_tls_error_timeout[] USER_RODATA = "timeout";
static const char user_tls_error_socket_state[] USER_RODATA = "invalid socket state";
static const char user_tls_error_reset[] USER_RODATA = "connection reset";
static const char user_tls_error_closed[] USER_RODATA = "connection closed";
static const char user_tls_error_would_block[] USER_RODATA = "would block";
static const char user_tls_error_io[] USER_RODATA = "i/o error";
static const char user_tls_error_buffer_overflow[] USER_RODATA = "buffer overflow";

static const char user_tls_selftest_tls13_ok[] USER_RODATA = "clienthello+serverhello+schedule ok";
static const char user_tls_selftest_tls13_client_hello_fail[] USER_RODATA = "clienthello build";
static const char user_tls_selftest_tls13_server_hello_fail[] USER_RODATA = "serverhello parse";
static const char user_tls_selftest_tls13_schedule_fail[] USER_RODATA = "key schedule";

static const uint8_t user_tls_empty_hash[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
    0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
    0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
};

static const uint8_t user_tls_zero_secret[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = { 0 };

static const uint8_t user_tls_hello_retry_random[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xcf, 0x21, 0xad, 0x74, 0xe5, 0x9a, 0x61, 0x11,
    0xbe, 0x1d, 0x8c, 0x02, 0x1e, 0x65, 0xb8, 0x91,
    0xc2, 0xa2, 0x11, 0x16, 0x7a, 0xbb, 0x8c, 0x5e,
    0x07, 0x9e, 0x09, 0xe2, 0xc8, 0xa8, 0x33, 0x9c
};

static const uint8_t user_tls_tls13_selftest_transcript_hash[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static const uint8_t user_tls_tls13_selftest_shared_secret[USER_TLS_X25519_KEY_SIZE] USER_RODATA = {
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf
};

static const uint8_t user_tls_tls13_selftest_client_hs_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0x03, 0xea, 0xaa, 0xfa, 0x76, 0x3d, 0x4b, 0xa6,
    0xbe, 0xe5, 0xf3, 0x32, 0x8f, 0xf6, 0x49, 0x4d
};

static const uint8_t user_tls_tls13_selftest_server_hs_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0x56, 0x36, 0x75, 0x02, 0x59, 0x7b, 0xd6, 0xa3,
    0x9e, 0x22, 0x3b, 0x48, 0x52, 0xc6, 0xe9, 0x2f
};

static const uint8_t user_tls_tls13_selftest_client_hs_iv[USER_TLS_GCM_IV_SIZE] USER_RODATA = {
    0x49, 0xb9, 0x23, 0x41, 0xf4, 0xc4, 0x6a, 0x82,
    0x34, 0x8c, 0xae, 0xdc
};

static const uint8_t user_tls_tls13_selftest_server_hs_iv[USER_TLS_GCM_IV_SIZE] USER_RODATA = {
    0x8b, 0x5c, 0xb7, 0x7a, 0xb4, 0xfd, 0x20, 0x26,
    0xff, 0x84, 0xc6, 0x22
};

static const uint8_t user_tls_tls13_selftest_client_app_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0x2a, 0xb5, 0x86, 0x99, 0x00, 0xc5, 0xfc, 0x03,
    0x00, 0x05, 0xc6, 0xf0, 0x2c, 0x2d, 0x92, 0x7c
};

static const uint8_t user_tls_tls13_selftest_server_app_key[USER_TLS_AES128_KEY_SIZE] USER_RODATA = {
    0xc7, 0x9b, 0xae, 0x30, 0x09, 0x9f, 0xe6, 0x6d,
    0xdf, 0x67, 0xd0, 0xd0, 0x72, 0xa0, 0x02, 0xa4
};

static const uint8_t user_tls_tls13_selftest_client_app_iv[USER_TLS_GCM_IV_SIZE] USER_RODATA = {
    0xa8, 0xa6, 0x05, 0x05, 0xa2, 0x51, 0xba, 0x8c,
    0x13, 0xbf, 0xea, 0xed
};

static const uint8_t user_tls_tls13_selftest_server_app_iv[USER_TLS_GCM_IV_SIZE] USER_RODATA = {
    0xe3, 0xe8, 0x00, 0xde, 0x07, 0xbe, 0xaf, 0x25,
    0xda, 0x88, 0x9c, 0xd2
};

static const uint8_t user_tls_tls13_selftest_server_finished[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0xfb, 0x03, 0xad, 0x01, 0xc0, 0xa2, 0x11, 0x8d,
    0x62, 0x80, 0xc0, 0x22, 0xf8, 0x33, 0xd1, 0x78,
    0x60, 0x42, 0xd9, 0x72, 0xa3, 0x78, 0x6d, 0x39,
    0xdf, 0x20, 0xdb, 0x6a, 0xda, 0x86, 0xbb, 0x9e
};

static const uint8_t user_tls_tls13_selftest_client_finished[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0x3d, 0x3f, 0xce, 0x76, 0x61, 0x83, 0x69, 0x8a,
    0x27, 0x03, 0x96, 0x6a, 0x91, 0x39, 0x28, 0xd4,
    0xa6, 0x69, 0x70, 0xf2, 0x48, 0xc7, 0x26, 0x9c,
    0x78, 0x28, 0x22, 0x1c, 0xb3, 0x0d, 0xd5, 0x99
};

static const uint8_t user_tls_tls13_selftest_server_hello[] USER_RODATA = {
    0x02, 0x00, 0x00, 0x76,
    0x03, 0x03,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x20,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x13, 0x01,
    0x00,
    0x00, 0x2e,
    0x00, 0x2b, 0x00, 0x02, 0x03, 0x04,
    0x00, 0x33, 0x00, 0x24, 0x00, 0x1d, 0x00, 0x20,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f
};

static USER_CODE void user_tls_copy_text(char* dst, uint32_t dst_len, const char* src) {
    uint32_t off = 0;

    if (!dst || dst_len == 0U) return;
    if (!src) src = "";
    while (src[off] != '\0' && off + 1U < dst_len) {
        dst[off] = src[off];
        off++;
    }
    dst[off] = '\0';
}

static USER_CODE void user_tls_copy_detail(char* detail, uint32_t detail_len, const char* text) {
    user_tls_copy_text(detail, detail_len, text);
}

static USER_CODE const char* user_tls_alert_detail(uint8_t description) {
    switch (description) {
        case 0U: return "alert close_notify";
        case 10U: return "alert unexpected_message";
        case 20U: return "alert bad_record_mac";
        case 40U: return "alert handshake_failure";
        case 47U: return "alert illegal_parameter";
        case 50U: return "alert decode_error";
        case 70U: return "alert protocol_version";
        case 71U: return "alert insufficient_security";
        case 80U: return "alert internal_error";
        case 109U: return "alert missing_extension";
        case 112U: return "alert unrecognized_name";
        case 120U: return "alert no_application_protocol";
        default: return "alert unknown";
    }
}

static USER_CODE void user_tls_debug_reset(void) {
    user_tls_debug_stage = USER_TLS_DEBUG_STAGE_IDLE;
    user_tls_debug_status_code = 0;
    user_tls_debug_detail_storage[0] = '\0';
}

static USER_CODE void user_tls_debug_note(uint32_t stage, const char* detail) {
    user_tls_debug_stage = stage;
    user_tls_copy_text(user_tls_debug_detail_storage, sizeof(user_tls_debug_detail_storage), detail);
}

static USER_CODE int user_tls_debug_fail(uint32_t stage, int status, const char* detail) {
    user_tls_debug_stage = stage;
    user_tls_debug_status_code = status;
    user_tls_copy_text(user_tls_debug_detail_storage, sizeof(user_tls_debug_detail_storage), detail);
    return status;
}

static USER_CODE int user_tls_debug_propagate(uint32_t stage, int status, const char* detail) {
    if (status >= 0) return status;
    if (user_tls_debug_status_code == 0) {
        user_tls_debug_stage = stage;
        user_tls_debug_status_code = status;
        user_tls_copy_text(user_tls_debug_detail_storage, sizeof(user_tls_debug_detail_storage), detail);
    }
    return status;
}

static USER_CODE uint16_t user_tls_load_be16(const uint8_t* src) {
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static USER_CODE uint32_t user_tls_load_be24(const uint8_t* src) {
    return ((uint32_t)src[0] << 16) | ((uint32_t)src[1] << 8) | src[2];
}

static USER_CODE void user_tls_store_be16(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static USER_CODE void user_tls_store_be24(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value >> 16);
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)value;
}

static USER_CODE int user_tls_buffer_append(uint8_t* buf, uint32_t cap, uint32_t* io_off,
                                            const void* data, uint32_t len) {
    uint32_t off = io_off ? *io_off : 0U;

    if (!buf || !io_off) return NET_ERR_INVALID;
    if (!data && len != 0U) return NET_ERR_INVALID;
    if (off > cap || len > cap - off) return USER_TLS_ERR_RECORD_OVERFLOW;
    if (len != 0U) memcpy(buf + off, data, len);
    *io_off = off + len;
    return 0;
}

static USER_CODE int user_tls_buffer_append_u8(uint8_t* buf, uint32_t cap, uint32_t* io_off, uint8_t value) {
    return user_tls_buffer_append(buf, cap, io_off, &value, 1U);
}

static USER_CODE int user_tls_buffer_append_u16(uint8_t* buf, uint32_t cap, uint32_t* io_off, uint16_t value) {
    uint8_t tmp[2];

    user_tls_store_be16(tmp, value);
    return user_tls_buffer_append(buf, cap, io_off, tmp, sizeof(tmp));
}

static USER_CODE int user_tls_buffer_append_u24(uint8_t* buf, uint32_t cap, uint32_t* io_off, uint32_t value) {
    uint8_t tmp[3];

    user_tls_store_be24(tmp, value);
    return user_tls_buffer_append(buf, cap, io_off, tmp, sizeof(tmp));
}

static USER_CODE int user_tls_cursor_read_u8(user_tls_cursor_t* cursor, uint8_t* out_value) {
    if (!cursor || !out_value || cursor->off >= cursor->len) return USER_TLS_ERR_PROTOCOL;
    *out_value = cursor->data[cursor->off++];
    return 0;
}

static USER_CODE int user_tls_cursor_read_u16(user_tls_cursor_t* cursor, uint16_t* out_value) {
    if (!cursor || !out_value || cursor->off + 2U > cursor->len) return USER_TLS_ERR_PROTOCOL;
    *out_value = user_tls_load_be16(cursor->data + cursor->off);
    cursor->off += 2U;
    return 0;
}

static USER_CODE int user_tls_cursor_read_u24(user_tls_cursor_t* cursor, uint32_t* out_value) {
    if (!cursor || !out_value || cursor->off + 3U > cursor->len) return USER_TLS_ERR_PROTOCOL;
    *out_value = user_tls_load_be24(cursor->data + cursor->off);
    cursor->off += 3U;
    return 0;
}

static USER_CODE int user_tls_cursor_read_bytes(user_tls_cursor_t* cursor, const uint8_t** out_bytes, uint32_t len) {
    if (!cursor || !out_bytes || cursor->off + len > cursor->len) return USER_TLS_ERR_PROTOCOL;
    *out_bytes = cursor->data + cursor->off;
    cursor->off += len;
    return 0;
}

static USER_CODE void user_tls_buffer_drop_prefix(uint8_t* buf, uint32_t* io_len, uint32_t consumed) {
    uint32_t len;

    if (!buf || !io_len) return;
    len = *io_len;
    if (consumed >= len) {
        *io_len = 0U;
        return;
    }
    for (uint32_t i = 0; i + consumed < len; i++) {
        buf[i] = buf[i + consumed];
    }
    *io_len = len - consumed;
}

static USER_CODE void user_tls_fill_sequence(uint8_t* out, uint32_t len, uint8_t start) {
    if (!out) return;
    for (uint32_t i = 0; i < len; i++) out[i] = (uint8_t)(start + i);
}

static USER_CODE int user_tls_is_all_zero(const uint8_t* data, uint32_t len) {
    uint8_t aggregate = 0U;

    if (!data) return 1;
    for (uint32_t i = 0; i < len; i++) aggregate |= data[i];
    return aggregate == 0U;
}

static USER_CODE int user_tls_copy_host(char* dst, uint32_t dst_len, const char* host) {
    uint32_t len;

    if (!dst || !host || dst_len == 0U) return NET_ERR_INVALID;
    len = (uint32_t)strlen(host);
    if (len == 0U || len + 1U > dst_len) return NET_ERR_INVALID;
    memcpy(dst, host, len);
    dst[len] = '\0';
    return 0;
}

static USER_CODE void user_tls_make_nonce(const uint8_t iv[USER_TLS_GCM_IV_SIZE], uint64_t seq,
                                          uint8_t out_nonce[USER_TLS_GCM_IV_SIZE]) {
    memcpy(out_nonce, iv, USER_TLS_GCM_IV_SIZE);
    for (uint32_t i = 0; i < 8U; i++) {
        out_nonce[USER_TLS_GCM_IV_SIZE - 1U - i] ^= (uint8_t)(seq & 0xffU);
        seq >>= 8;
    }
}

static USER_CODE void user_tls_transcript_hash_snapshot(const user_tls_transcript_hash_t* transcript,
                                                        uint8_t out_hash[USER_TLS_SHA256_DIGEST_SIZE]) {
    user_tls_transcript_hash_t copy;

    if (!transcript || !out_hash) return;
    copy = *transcript;
    user_tls_transcript_final(&copy, out_hash);
}

static USER_CODE int user_tls_derive_secret(const uint8_t* secret, const char* label,
                                            const uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE],
                                            uint8_t out_secret[USER_TLS_SHA256_DIGEST_SIZE]) {
    return user_tls_hkdf_expand_label(secret, USER_TLS_SHA256_DIGEST_SIZE, label,
                                      transcript_hash, USER_TLS_SHA256_DIGEST_SIZE,
                                      out_secret, USER_TLS_SHA256_DIGEST_SIZE);
}

static USER_CODE int user_tls_derive_key_iv(const uint8_t traffic_secret[USER_TLS_SHA256_DIGEST_SIZE],
                                            uint8_t out_key[USER_TLS_AES128_KEY_SIZE],
                                            uint8_t out_iv[USER_TLS_GCM_IV_SIZE]) {
    if (user_tls_hkdf_expand_label(traffic_secret, USER_TLS_SHA256_DIGEST_SIZE, "key",
                                   (const void*)0, 0U, out_key, USER_TLS_AES128_KEY_SIZE) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_hkdf_expand_label(traffic_secret, USER_TLS_SHA256_DIGEST_SIZE, "iv",
                                   (const void*)0, 0U, out_iv, USER_TLS_GCM_IV_SIZE) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    return 0;
}

static USER_CODE int user_tls_compute_finished_verify_data(const uint8_t finished_key[USER_TLS_SHA256_DIGEST_SIZE],
                                                           const uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE],
                                                           uint8_t out_verify[USER_TLS_SHA256_DIGEST_SIZE]) {
    if (user_tls_hmac_sha256(finished_key, USER_TLS_SHA256_DIGEST_SIZE,
                             transcript_hash, USER_TLS_SHA256_DIGEST_SIZE,
                             out_verify) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    return 0;
}

static USER_CODE int user_tls_derive_handshake_schedule(user_tls_key_schedule_t* keys,
                                                        const uint8_t shared_secret[USER_TLS_X25519_KEY_SIZE],
                                                        const uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE]) {
    uint8_t early_secret[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t derived_secret[USER_TLS_SHA256_DIGEST_SIZE];

    if (!keys || !shared_secret || !transcript_hash) return NET_ERR_INVALID;
    memset(keys, 0, sizeof(*keys));

    if (user_tls_hkdf_extract(user_tls_zero_secret, USER_TLS_SHA256_DIGEST_SIZE,
                              user_tls_zero_secret, USER_TLS_SHA256_DIGEST_SIZE,
                              early_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_derive_secret(early_secret, "derived", user_tls_empty_hash, derived_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_hkdf_extract(derived_secret, USER_TLS_SHA256_DIGEST_SIZE,
                              shared_secret, USER_TLS_X25519_KEY_SIZE,
                              keys->handshake_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_derive_secret(keys->handshake_secret, "c hs traffic", transcript_hash,
                               keys->client_handshake_secret) != 0 ||
        user_tls_derive_secret(keys->handshake_secret, "s hs traffic", transcript_hash,
                               keys->server_handshake_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_derive_key_iv(keys->client_handshake_secret,
                               keys->client_handshake_key, keys->client_handshake_iv) != 0 ||
        user_tls_derive_key_iv(keys->server_handshake_secret,
                               keys->server_handshake_key, keys->server_handshake_iv) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_hkdf_expand_label(keys->client_handshake_secret, USER_TLS_SHA256_DIGEST_SIZE,
                                   "finished", (const void*)0, 0U,
                                   keys->client_finished_key, USER_TLS_SHA256_DIGEST_SIZE) != 0 ||
        user_tls_hkdf_expand_label(keys->server_handshake_secret, USER_TLS_SHA256_DIGEST_SIZE,
                                   "finished", (const void*)0, 0U,
                                   keys->server_finished_key, USER_TLS_SHA256_DIGEST_SIZE) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_derive_secret(keys->handshake_secret, "derived", user_tls_empty_hash, derived_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_hkdf_extract(derived_secret, USER_TLS_SHA256_DIGEST_SIZE,
                              user_tls_zero_secret, USER_TLS_SHA256_DIGEST_SIZE,
                              keys->master_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    return 0;
}

static USER_CODE int user_tls_derive_application_schedule(user_tls_key_schedule_t* keys,
                                                          const uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE]) {
    if (!keys || !transcript_hash) return NET_ERR_INVALID;
    if (user_tls_derive_secret(keys->master_secret, "c ap traffic", transcript_hash,
                               keys->client_application_secret) != 0 ||
        user_tls_derive_secret(keys->master_secret, "s ap traffic", transcript_hash,
                               keys->server_application_secret) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (user_tls_derive_key_iv(keys->client_application_secret,
                               keys->client_application_key, keys->client_application_iv) != 0 ||
        user_tls_derive_key_iv(keys->server_application_secret,
                               keys->server_application_key, keys->server_application_iv) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    return 0;
}

static USER_CODE int user_tls_allocate_client(void) {
    user_tls_client_t* client;

    if (user_tls_client_state) return 0;
    client = &user_tls_client_storage;
    memset(client, 0, sizeof(*client));
    client->socket_handle = -1;
    client->record_buf = user_tls_record_storage;
    client->plain_buf = user_tls_plain_storage;
    client->message_buf = user_tls_message_storage;
    client->cert_buf = user_tls_cert_storage;
    user_tls_client_state = client;
    return 0;
}

static USER_CODE void user_tls_free_client(void) {
    user_tls_client_t* client = user_tls_client_state;

    if (!client) return;
    memset(client, 0, sizeof(*client));
    user_tls_client_state = (user_tls_client_t*)0;
}

static USER_CODE void user_tls_reset_client_runtime(user_tls_client_t* client) {
    uint8_t* record_buf;
    uint8_t* plain_buf;
    uint8_t* message_buf;
    uint8_t* cert_buf;

    if (!client) return;
    record_buf = client->record_buf;
    plain_buf = client->plain_buf;
    message_buf = client->message_buf;
    cert_buf = client->cert_buf;
    memset(client, 0, sizeof(*client));
    client->record_buf = record_buf;
    client->plain_buf = plain_buf;
    client->message_buf = message_buf;
    client->cert_buf = cert_buf;
    client->socket_handle = -1;
}

static USER_CODE int user_tls_socket_read_exact(user_tls_client_t* client, uint8_t* out, uint32_t len) {
    uint32_t received = 0U;
    uint32_t started = user_uptime_ticks();
    uint32_t last_progress = started;

    if (!client || !out || len == 0U) return len == 0U ? 0 : NET_ERR_INVALID;

    while (received < len) {
        int available = user_net_available(client->socket_handle);

        if (available == NET_ERR_CLOSED) return NET_ERR_CLOSED;
        if (available < 0) return available;
        if (available > 0) {
            uint32_t to_read = len - received;
            int got;

            if ((uint32_t)available < to_read) to_read = (uint32_t)available;
            got = user_net_recv(client->socket_handle, out + received, (uint16_t)to_read);
            if (got == NET_ERR_WOULD_BLOCK) {
                user_yield();
                continue;
            }
            if (got < 0) return got;
            if (got == 0) return NET_ERR_TIMEOUT;
            received += (uint32_t)got;
            last_progress = user_uptime_ticks();
        }

        if (user_uptime_ticks() - last_progress > USER_TLS_RECORD_READ_IDLE_TIMEOUT) return NET_ERR_TIMEOUT;
        if (user_uptime_ticks() - started > USER_TLS_RECORD_READ_TOTAL_TIMEOUT) return NET_ERR_TIMEOUT;
        user_yield();
    }
    return 0;
}

static USER_CODE int user_tls_send_plain_record(user_tls_client_t* client, uint8_t content_type,
                                                uint16_t legacy_version,
                                                const uint8_t* payload, uint16_t payload_len) {
    uint32_t total_len;
    int status;

    if (!client) return NET_ERR_INVALID;
    if (!payload && payload_len != 0U) return NET_ERR_INVALID;
    if ((uint32_t)payload_len > USER_TLS_RECORD_CIPHERTEXT_MAX) return USER_TLS_ERR_RECORD_OVERFLOW;

    client->record_buf[0] = content_type;
    user_tls_store_be16(client->record_buf + 1U, legacy_version);
    user_tls_store_be16(client->record_buf + 3U, payload_len);
    if (payload_len != 0U) memcpy(client->record_buf + USER_TLS_RECORD_HEADER_SIZE, payload, payload_len);
    total_len = USER_TLS_RECORD_HEADER_SIZE + payload_len;
    status = user_net_send(client->socket_handle, client->record_buf, (uint16_t)total_len);
    if (status < 0) return status;
    return status == (int)total_len ? 0 : USER_TLS_ERR_PROTOCOL;
}

static USER_CODE int user_tls_select_write_keys(user_tls_client_t* client,
                                                const uint8_t** out_key, const uint8_t** out_iv,
                                                uint64_t** out_seq) {
    if (!client || !out_key || !out_iv || !out_seq) return NET_ERR_INVALID;
    if (!client->write_has_keys) return USER_TLS_ERR_NOT_CONNECTED;
    if (client->write_use_application_keys) {
        *out_key = client->keys.client_application_key;
        *out_iv = client->keys.client_application_iv;
        *out_seq = &client->application_write_seq;
        return 0;
    }
    *out_key = client->keys.client_handshake_key;
    *out_iv = client->keys.client_handshake_iv;
    *out_seq = &client->handshake_write_seq;
    return 0;
}

static USER_CODE int user_tls_select_read_keys(user_tls_client_t* client,
                                               const uint8_t** out_key, const uint8_t** out_iv,
                                               uint64_t** out_seq) {
    if (!client || !out_key || !out_iv || !out_seq) return NET_ERR_INVALID;
    if (!client->read_has_keys) return USER_TLS_ERR_NOT_CONNECTED;
    if (client->read_use_application_keys) {
        *out_key = client->keys.server_application_key;
        *out_iv = client->keys.server_application_iv;
        *out_seq = &client->application_read_seq;
        return 0;
    }
    *out_key = client->keys.server_handshake_key;
    *out_iv = client->keys.server_handshake_iv;
    *out_seq = &client->handshake_read_seq;
    return 0;
}

static USER_CODE int user_tls_send_protected_record(user_tls_client_t* client, uint8_t inner_type,
                                                    const uint8_t* payload, uint32_t payload_len) {
    const uint8_t* key;
    const uint8_t* iv;
    uint64_t* seq;
    uint8_t nonce[USER_TLS_GCM_IV_SIZE];
    uint8_t tag[USER_TLS_GCM_TAG_SIZE];
    uint32_t inner_len;
    uint16_t outer_len;
    int status;

    if (!client) return NET_ERR_INVALID;
    if (!payload && payload_len != 0U) return NET_ERR_INVALID;
    if (payload_len > USER_TLS_RECORD_PLAINTEXT_MAX) return USER_TLS_ERR_RECORD_OVERFLOW;
    status = user_tls_select_write_keys(client, &key, &iv, &seq);
    if (status != 0) return status;

    inner_len = payload_len + 1U;
    memcpy(client->plain_buf, payload, payload_len);
    client->plain_buf[payload_len] = inner_type;
    outer_len = (uint16_t)(inner_len + USER_TLS_GCM_TAG_SIZE);

    client->record_buf[0] = USER_TLS_CONTENT_APPLICATION_DATA;
    user_tls_store_be16(client->record_buf + 1U, USER_TLS_VERSION_TLS12);
    user_tls_store_be16(client->record_buf + 3U, outer_len);
    user_tls_make_nonce(iv, *seq, nonce);

    if (user_tls_aes128_gcm_encrypt(key, nonce,
                                    client->record_buf, USER_TLS_RECORD_HEADER_SIZE,
                                    client->plain_buf, inner_len,
                                    client->record_buf + USER_TLS_RECORD_HEADER_SIZE,
                                    tag) != 0) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_RECORD_ENCRYPT, USER_TLS_ERR_CRYPTO,
                                   "aes-gcm encrypt");
    }

    memcpy(client->record_buf + USER_TLS_RECORD_HEADER_SIZE + inner_len, tag, sizeof(tag));
    status = user_net_send(client->socket_handle, client->record_buf,
                           (uint16_t)(USER_TLS_RECORD_HEADER_SIZE + outer_len));
    if (status < 0) return status;
    if (status != (int)(USER_TLS_RECORD_HEADER_SIZE + outer_len)) return USER_TLS_ERR_PROTOCOL;
    (*seq)++;
    return 0;
}

static USER_CODE int user_tls_handle_alert(user_tls_client_t* client,
                                           const uint8_t* payload, uint16_t payload_len) {
    if (!client || !payload || payload_len != 2U) return USER_TLS_ERR_ALERT;
    client->last_alert_level = payload[0];
    client->last_alert_description = payload[1];
    if (payload[1] == 0U) {
        client->peer_closed = 1U;
        user_tls_copy_text(user_tls_debug_detail_storage, sizeof(user_tls_debug_detail_storage),
                           user_tls_alert_detail(payload[1]));
        return NET_ERR_CLOSED;
    }
    return user_tls_debug_fail(user_tls_debug_stage, USER_TLS_ERR_ALERT, user_tls_alert_detail(payload[1]));
}

static USER_CODE int user_tls_decrypt_record(user_tls_client_t* client,
                                             const uint8_t header[USER_TLS_RECORD_HEADER_SIZE],
                                             const uint8_t* ciphertext, uint16_t ciphertext_len,
                                             uint8_t* out_inner_type, const uint8_t** out_plain,
                                             uint16_t* out_plain_len) {
    const uint8_t* key;
    const uint8_t* iv;
    uint64_t* seq;
    uint8_t nonce[USER_TLS_GCM_IV_SIZE];
    const uint8_t* tag;
    uint16_t enc_len;
    int status;
    uint32_t pos;

    if (!client || !header || !ciphertext || !out_inner_type || !out_plain || !out_plain_len) {
        return NET_ERR_INVALID;
    }
    if (ciphertext_len < USER_TLS_GCM_TAG_SIZE) return USER_TLS_ERR_PROTOCOL;
    status = user_tls_select_read_keys(client, &key, &iv, &seq);
    if (status != 0) return status;

    enc_len = (uint16_t)(ciphertext_len - USER_TLS_GCM_TAG_SIZE);
    tag = ciphertext + enc_len;
    user_tls_make_nonce(iv, *seq, nonce);

    if (user_tls_aes128_gcm_decrypt(key, nonce,
                                    header, USER_TLS_RECORD_HEADER_SIZE,
                                    ciphertext, enc_len, tag,
                                    client->plain_buf) != 0) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_RECORD_DECRYPT, USER_TLS_ERR_CRYPTO,
                                   "aes-gcm decrypt");
    }
    (*seq)++;

    pos = enc_len;
    while (pos != 0U && client->plain_buf[pos - 1U] == 0U) pos--;
    if (pos == 0U) return USER_TLS_ERR_PROTOCOL;

    *out_inner_type = client->plain_buf[pos - 1U];
    *out_plain = client->plain_buf;
    *out_plain_len = (uint16_t)(pos - 1U);
    return 0;
}

static USER_CODE int user_tls_read_record_content(user_tls_client_t* client,
                                                  uint8_t* out_content_type,
                                                  const uint8_t** out_payload,
                                                  uint16_t* out_payload_len) {
    uint8_t header[USER_TLS_RECORD_HEADER_SIZE];
    uint8_t inner_type = 0U;
    uint16_t record_len;
    int status;

    if (!client || !out_content_type || !out_payload || !out_payload_len) return NET_ERR_INVALID;

    status = user_tls_socket_read_exact(client, header, sizeof(header));
    if (status < 0) return status;
    record_len = user_tls_load_be16(header + 3U);
    if (record_len > USER_TLS_RECORD_CIPHERTEXT_MAX) return USER_TLS_ERR_RECORD_OVERFLOW;

    status = user_tls_socket_read_exact(client, client->record_buf, record_len);
    if (status < 0) return status;

    if (header[0] == USER_TLS_CONTENT_CHANGE_CIPHER_SPEC) {
        if (record_len == 1U && client->record_buf[0] == 0x01U) return USER_TLS_INTERNAL_SKIP;
        return USER_TLS_ERR_PROTOCOL;
    }

    if (!client->read_has_keys) {
        if (header[0] == USER_TLS_CONTENT_ALERT) return user_tls_handle_alert(client, client->record_buf, record_len);
        if (header[0] != USER_TLS_CONTENT_HANDSHAKE) return USER_TLS_ERR_PROTOCOL;
        *out_content_type = USER_TLS_CONTENT_HANDSHAKE;
        *out_payload = client->record_buf;
        *out_payload_len = record_len;
        return 0;
    }

    if (header[0] == USER_TLS_CONTENT_ALERT) return user_tls_handle_alert(client, client->record_buf, record_len);
    if (header[0] != USER_TLS_CONTENT_APPLICATION_DATA) return USER_TLS_ERR_PROTOCOL;

    status = user_tls_decrypt_record(client, header, client->record_buf, record_len,
                                     &inner_type, out_payload, out_payload_len);
    if (status < 0) return status;
    if (inner_type == USER_TLS_CONTENT_ALERT) return user_tls_handle_alert(client, *out_payload, *out_payload_len);
    if (inner_type != USER_TLS_CONTENT_HANDSHAKE && inner_type != USER_TLS_CONTENT_APPLICATION_DATA) {
        return USER_TLS_ERR_PROTOCOL;
    }
    if (*out_payload_len == 0U) return USER_TLS_INTERNAL_SKIP;
    *out_content_type = inner_type;
    return 0;
}

static USER_CODE int user_tls_next_handshake_message(user_tls_client_t* client,
                                                     const uint8_t** out_raw,
                                                     uint32_t* out_raw_len,
                                                     uint8_t* out_type,
                                                     const uint8_t** out_body,
                                                     uint32_t* out_body_len) {
    while (1) {
        if (client->message_buf_len >= 4U) {
            uint32_t msg_len = user_tls_load_be24(client->message_buf + 1U);
            uint32_t total_len = 4U + msg_len;

            if (total_len <= client->message_buf_len) {
                *out_raw = client->message_buf;
                *out_raw_len = total_len;
                *out_type = client->message_buf[0];
                *out_body = client->message_buf + 4U;
                *out_body_len = msg_len;
                return 0;
            }
        }

        {
            uint8_t content_type = 0U;
            const uint8_t* payload = (const uint8_t*)0;
            uint16_t payload_len = 0U;
            int status = user_tls_read_record_content(client, &content_type, &payload, &payload_len);

            if (status == USER_TLS_INTERNAL_SKIP) continue;
            if (status < 0) return status;
            if (content_type != USER_TLS_CONTENT_HANDSHAKE) return USER_TLS_ERR_PROTOCOL;
            if (client->message_buf_len + payload_len > USER_TLS_HANDSHAKE_BUFFER_SIZE) {
                return USER_TLS_ERR_RECORD_OVERFLOW;
            }
            memcpy(client->message_buf + client->message_buf_len, payload, payload_len);
            client->message_buf_len += payload_len;
        }
    }
}

static USER_CODE int user_tls_find_extension(const uint8_t* ext_bytes, uint32_t ext_len,
                                             uint16_t extension_type,
                                             const uint8_t** out_data, uint16_t* out_len) {
    user_tls_cursor_t cursor;

    if (out_data) *out_data = (const uint8_t*)0;
    if (out_len) *out_len = 0U;
    if (!ext_bytes) return NET_ERR_INVALID;

    cursor.data = ext_bytes;
    cursor.len = ext_len;
    cursor.off = 0U;
    while (cursor.off < cursor.len) {
        uint16_t ext_type = 0U;
        uint16_t data_len = 0U;
        const uint8_t* data = (const uint8_t*)0;

        if (user_tls_cursor_read_u16(&cursor, &ext_type) != 0 ||
            user_tls_cursor_read_u16(&cursor, &data_len) != 0 ||
            user_tls_cursor_read_bytes(&cursor, &data, data_len) != 0) {
            return USER_TLS_ERR_PROTOCOL;
        }
        if (ext_type == extension_type) {
            if (out_data) *out_data = data;
            if (out_len) *out_len = data_len;
            return 0;
        }
    }
    return USER_TLS_ERR_PROTOCOL;
}

static USER_CODE int user_tls_build_client_hello(uint8_t* out_buf, uint32_t out_cap, uint32_t* out_len,
                                                 const char* host,
                                                 const uint8_t client_random[USER_TLS_SHA256_DIGEST_SIZE],
                                                 const uint8_t session_id[USER_TLS_SHA256_DIGEST_SIZE],
                                                 const uint8_t client_public[USER_TLS_X25519_KEY_SIZE]) {
    uint32_t off = 0U;
    uint32_t body_start;
    uint32_t ext_len_off;
    uint32_t ext_start;
    uint32_t host_len;

    if (!out_buf || !out_len || !host || !client_random || !session_id || !client_public) {
        return NET_ERR_INVALID;
    }
    host_len = (uint32_t)strlen(host);
    if (host_len == 0U || host_len > USER_TLS_MAX_HOSTNAME_LEN) return NET_ERR_INVALID;

    if (user_tls_buffer_append_u8(out_buf, out_cap, &off, USER_TLS_HANDSHAKE_CLIENT_HELLO) != 0 ||
        user_tls_buffer_append_u24(out_buf, out_cap, &off, 0U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_VERSION_TLS12) != 0 ||
        user_tls_buffer_append(out_buf, out_cap, &off, client_random, USER_TLS_SHA256_DIGEST_SIZE) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, USER_TLS_SHA256_DIGEST_SIZE) != 0 ||
        user_tls_buffer_append(out_buf, out_cap, &off, session_id, USER_TLS_SHA256_DIGEST_SIZE) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 2U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_CIPHER_TLS_AES_128_GCM_SHA256) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, 1U) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, 0U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 0U) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    body_start = 4U;
    ext_len_off = off - 2U;
    ext_start = off;

    if (user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_EXT_SERVER_NAME) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, (uint16_t)(5U + host_len)) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, (uint16_t)(3U + host_len)) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, 0U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, (uint16_t)host_len) != 0 ||
        user_tls_buffer_append(out_buf, out_cap, &off, host, host_len) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    if (user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_EXT_SUPPORTED_GROUPS) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 4U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 2U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_GROUP_X25519) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    if (user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_EXT_SIGNATURE_ALGORITHMS) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 8U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 6U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_SIG_ECDSA_SECP256R1_SHA256) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_SIG_RSA_PSS_RSAE_SHA256) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_SIG_RSA_PSS_PSS_SHA256) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    if (user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_EXT_SUPPORTED_VERSIONS) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 3U) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, 2U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_VERSION_TLS13) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    if (user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_EXT_PSK_KEY_EXCHANGE_MODES) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 2U) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, 1U) != 0 ||
        user_tls_buffer_append_u8(out_buf, out_cap, &off, 1U) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    if (user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_EXT_KEY_SHARE) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 38U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, 36U) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_GROUP_X25519) != 0 ||
        user_tls_buffer_append_u16(out_buf, out_cap, &off, USER_TLS_X25519_KEY_SIZE) != 0 ||
        user_tls_buffer_append(out_buf, out_cap, &off, client_public, USER_TLS_X25519_KEY_SIZE) != 0) {
        return USER_TLS_ERR_RECORD_OVERFLOW;
    }

    user_tls_store_be16(out_buf + ext_len_off, (uint16_t)(off - ext_start));
    user_tls_store_be24(out_buf + 1U, off - body_start);
    *out_len = off;
    return 0;
}

static USER_CODE int user_tls_parse_server_hello(const uint8_t* raw, uint32_t raw_len,
                                                 const uint8_t expected_session_id[USER_TLS_SHA256_DIGEST_SIZE],
                                                 uint8_t out_server_random[USER_TLS_SHA256_DIGEST_SIZE],
                                                 uint8_t out_server_public[USER_TLS_X25519_KEY_SIZE]) {
    user_tls_cursor_t cursor;
    uint16_t legacy_version = 0U;
    uint8_t session_len = 0U;
    uint16_t cipher_suite = 0U;
    uint8_t compression = 0U;
    uint16_t ext_total_len = 0U;
    const uint8_t* server_random = (const uint8_t*)0;
    const uint8_t* session_bytes = (const uint8_t*)0;
    int saw_supported_versions = 0;
    int saw_key_share = 0;

    if (!raw || !expected_session_id || !out_server_random || !out_server_public) return NET_ERR_INVALID;
    if (raw_len < 4U || raw[0] != USER_TLS_HANDSHAKE_SERVER_HELLO) return USER_TLS_ERR_PROTOCOL;
    if (user_tls_load_be24(raw + 1U) + 4U != raw_len) return USER_TLS_ERR_PROTOCOL;

    cursor.data = raw + 4U;
    cursor.len = raw_len - 4U;
    cursor.off = 0U;

    if (user_tls_cursor_read_u16(&cursor, &legacy_version) != 0 ||
        user_tls_cursor_read_bytes(&cursor, &server_random, USER_TLS_SHA256_DIGEST_SIZE) != 0 ||
        user_tls_cursor_read_u8(&cursor, &session_len) != 0 ||
        session_len != USER_TLS_SHA256_DIGEST_SIZE ||
        user_tls_cursor_read_bytes(&cursor, &session_bytes, session_len) != 0 ||
        user_tls_cursor_read_u16(&cursor, &cipher_suite) != 0 ||
        user_tls_cursor_read_u8(&cursor, &compression) != 0 ||
        user_tls_cursor_read_u16(&cursor, &ext_total_len) != 0) {
        return USER_TLS_ERR_PROTOCOL;
    }
    if (legacy_version != USER_TLS_VERSION_TLS12 ||
        cipher_suite != USER_TLS_CIPHER_TLS_AES_128_GCM_SHA256 ||
        compression != 0U ||
        memcmp(session_bytes, expected_session_id, USER_TLS_SHA256_DIGEST_SIZE) != 0) {
        return USER_TLS_ERR_PROTOCOL;
    }
    if (memcmp(server_random, user_tls_hello_retry_random, USER_TLS_SHA256_DIGEST_SIZE) == 0) {
        return USER_TLS_ERR_PROTOCOL;
    }
    if (cursor.off + ext_total_len != cursor.len) return USER_TLS_ERR_PROTOCOL;

    while (cursor.off < cursor.len) {
        uint16_t ext_type = 0U;
        uint16_t data_len = 0U;
        const uint8_t* data = (const uint8_t*)0;

        if (user_tls_cursor_read_u16(&cursor, &ext_type) != 0 ||
            user_tls_cursor_read_u16(&cursor, &data_len) != 0 ||
            user_tls_cursor_read_bytes(&cursor, &data, data_len) != 0) {
            return USER_TLS_ERR_PROTOCOL;
        }

        if (ext_type == USER_TLS_EXT_SUPPORTED_VERSIONS) {
            if (data_len != 2U || user_tls_load_be16(data) != USER_TLS_VERSION_TLS13) return USER_TLS_ERR_PROTOCOL;
            saw_supported_versions = 1;
        } else if (ext_type == USER_TLS_EXT_KEY_SHARE) {
            if (data_len != 36U) return USER_TLS_ERR_PROTOCOL;
            if (user_tls_load_be16(data) != USER_TLS_GROUP_X25519 ||
                user_tls_load_be16(data + 2U) != USER_TLS_X25519_KEY_SIZE) {
                return USER_TLS_ERR_PROTOCOL;
            }
            memcpy(out_server_public, data + 4U, USER_TLS_X25519_KEY_SIZE);
            saw_key_share = 1;
        }
    }

    if (!saw_supported_versions || !saw_key_share) return USER_TLS_ERR_PROTOCOL;
    memcpy(out_server_random, server_random, USER_TLS_SHA256_DIGEST_SIZE);
    return 0;
}

static USER_CODE int user_tls_parse_encrypted_extensions(const uint8_t* raw, uint32_t raw_len) {
    user_tls_cursor_t cursor;
    uint16_t ext_total_len = 0U;

    if (!raw || raw_len < 4U || raw[0] != USER_TLS_HANDSHAKE_ENCRYPTED_EXTENSIONS) return USER_TLS_ERR_PROTOCOL;
    if (user_tls_load_be24(raw + 1U) + 4U != raw_len) return USER_TLS_ERR_PROTOCOL;

    cursor.data = raw + 4U;
    cursor.len = raw_len - 4U;
    cursor.off = 0U;
    if (user_tls_cursor_read_u16(&cursor, &ext_total_len) != 0) return USER_TLS_ERR_PROTOCOL;
    if (cursor.off + ext_total_len != cursor.len) return USER_TLS_ERR_PROTOCOL;

    while (cursor.off < cursor.len) {
        uint16_t ext_type = 0U;
        uint16_t data_len = 0U;
        const uint8_t* data = (const uint8_t*)0;

        if (user_tls_cursor_read_u16(&cursor, &ext_type) != 0 ||
            user_tls_cursor_read_u16(&cursor, &data_len) != 0 ||
            user_tls_cursor_read_bytes(&cursor, &data, data_len) != 0) {
            return USER_TLS_ERR_PROTOCOL;
        }
        (void)ext_type;
        (void)data;
    }
    return 0;
}

static USER_CODE int user_tls_process_certificate(user_tls_client_t* client,
                                                  const uint8_t* raw, uint32_t raw_len) {
    user_tls_cursor_t cursor;
    uint8_t context_len = 0U;
    uint32_t cert_list_len = 0U;
    uint8_t spki_hash[USER_TLS_SHA256_DIGEST_SIZE];
    int saw_leaf = 0;

    if (!client || !raw || raw_len < 4U || raw[0] != USER_TLS_HANDSHAKE_CERTIFICATE) {
        return USER_TLS_ERR_CERTIFICATE;
    }
    if (user_tls_load_be24(raw + 1U) + 4U != raw_len) return USER_TLS_ERR_CERTIFICATE;

    cursor.data = raw + 4U;
    cursor.len = raw_len - 4U;
    cursor.off = 0U;

    if (user_tls_cursor_read_u8(&cursor, &context_len) != 0 || context_len != 0U) return USER_TLS_ERR_CERTIFICATE;
    if (user_tls_cursor_read_u24(&cursor, &cert_list_len) != 0) return USER_TLS_ERR_CERTIFICATE;
    if (cursor.off + cert_list_len != cursor.len || cert_list_len == 0U) return USER_TLS_ERR_CERTIFICATE;

    while (cursor.off < cursor.len) {
        uint32_t cert_len = 0U;
        uint16_t ext_len = 0U;
        const uint8_t* cert_data = (const uint8_t*)0;
        const uint8_t* entry_ext = (const uint8_t*)0;

        if (user_tls_cursor_read_u24(&cursor, &cert_len) != 0 ||
            cert_len == 0U ||
            user_tls_cursor_read_bytes(&cursor, &cert_data, cert_len) != 0 ||
            user_tls_cursor_read_u16(&cursor, &ext_len) != 0 ||
            user_tls_cursor_read_bytes(&cursor, &entry_ext, ext_len) != 0) {
            return USER_TLS_ERR_CERTIFICATE;
        }
        (void)entry_ext;

        if (!saw_leaf) {
            if (cert_len > USER_TLS_CERT_BUFFER_SIZE) return USER_TLS_ERR_CERTIFICATE;
            memcpy(client->cert_buf, cert_data, cert_len);
            if (user_tls_x509_parse_leaf(&client->server_cert, client->cert_buf, cert_len) != 0) {
                return USER_TLS_ERR_CERTIFICATE;
            }
            if (user_tls_x509_hostname_matches(&client->server_cert, client->server_name) != 0) {
                return USER_TLS_ERR_HOSTNAME;
            }
            if (client->validated_unix_time == 0U ||
                user_tls_x509_is_valid_at(&client->server_cert, client->validated_unix_time) != 0) {
                return USER_TLS_ERR_CERT_TIME;
            }
            if (user_tls_x509_spki_sha256(&client->server_cert, spki_hash) != 0) {
                return USER_TLS_ERR_CERTIFICATE;
            }
            if (user_tls_pins_match_host(client->server_name, spki_hash) != 0) {
                return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_CERTIFICATE, USER_TLS_ERR_PIN, "pin mismatch");
            }
            saw_leaf = 1;
        }
    }

    return saw_leaf ? 0 : USER_TLS_ERR_CERTIFICATE;
}

static USER_CODE int user_tls_process_certificate_verify(user_tls_client_t* client,
                                                         const uint8_t* raw, uint32_t raw_len) {
    user_tls_cursor_t cursor;
    uint16_t sig_alg = 0U;
    uint16_t sig_len = 0U;
    const uint8_t* signature = (const uint8_t*)0;
    uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t signed_content[64U + 33U + 1U + USER_TLS_SHA256_DIGEST_SIZE];
    static const char context_string[] USER_RODATA = "TLS 1.3, server CertificateVerify";
    uint32_t content_len;

    if (!client || !raw || raw_len < 4U || raw[0] != USER_TLS_HANDSHAKE_CERTIFICATE_VERIFY) {
        return USER_TLS_ERR_CERTIFICATE;
    }
    if (user_tls_load_be24(raw + 1U) + 4U != raw_len) return USER_TLS_ERR_CERTIFICATE;

    cursor.data = raw + 4U;
    cursor.len = raw_len - 4U;
    cursor.off = 0U;
    if (user_tls_cursor_read_u16(&cursor, &sig_alg) != 0 ||
        user_tls_cursor_read_u16(&cursor, &sig_len) != 0 ||
        user_tls_cursor_read_bytes(&cursor, &signature, sig_len) != 0 ||
        cursor.off != cursor.len) {
        return USER_TLS_ERR_CERTIFICATE;
    }
    memset(signed_content, 0x20, 64U);
    content_len = 64U;
    memcpy(signed_content + content_len, context_string, sizeof(context_string) - 1U);
    content_len += (uint32_t)(sizeof(context_string) - 1U);
    signed_content[content_len++] = 0U;
    user_tls_transcript_hash_snapshot(&client->transcript, transcript_hash);
    memcpy(signed_content + content_len, transcript_hash, sizeof(transcript_hash));
    content_len += sizeof(transcript_hash);

    if (sig_alg == USER_TLS_SIG_ECDSA_SECP256R1_SHA256) {
        if (client->server_cert.key_type != USER_TLS_X509_KEY_EC_P256 ||
            client->server_cert.ec_public_x.len != 32U ||
            client->server_cert.ec_public_y.len != 32U ||
            user_tls_ecdsa_p256_sha256_verify(client->server_cert.ec_public_x.data,
                                              client->server_cert.ec_public_y.data,
                                              signature, sig_len,
                                              signed_content, content_len) != 0) {
            return USER_TLS_ERR_CERTIFICATE;
        }
        return 0;
    }

    if (sig_alg != USER_TLS_SIG_RSA_PSS_RSAE_SHA256 &&
        sig_alg != USER_TLS_SIG_RSA_PSS_PSS_SHA256) {
        return USER_TLS_ERR_CERTIFICATE;
    }
    if (client->server_cert.key_type != USER_TLS_X509_KEY_RSA ||
        user_tls_rsa_pss_sha256_verify(client->server_cert.rsa_modulus.data,
                                       client->server_cert.rsa_modulus.len,
                                       client->server_cert.rsa_exponent,
                                       signature, sig_len,
                                       signed_content, content_len) != 0) {
        return USER_TLS_ERR_CERTIFICATE;
    }
    return 0;
}

static USER_CODE int user_tls_process_server_finished(user_tls_client_t* client,
                                                      const uint8_t* raw, uint32_t raw_len) {
    uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t expected[USER_TLS_SHA256_DIGEST_SIZE];
    const uint8_t* verify_data;

    if (!client || !raw || raw_len != 4U + USER_TLS_SHA256_DIGEST_SIZE ||
        raw[0] != USER_TLS_HANDSHAKE_FINISHED ||
        user_tls_load_be24(raw + 1U) != USER_TLS_SHA256_DIGEST_SIZE) {
        return USER_TLS_ERR_CRYPTO;
    }

    verify_data = raw + 4U;
    user_tls_transcript_hash_snapshot(&client->transcript, transcript_hash);
    if (user_tls_compute_finished_verify_data(client->keys.server_finished_key,
                                              transcript_hash, expected) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }
    if (!user_memeq_consttime(expected, verify_data, USER_TLS_SHA256_DIGEST_SIZE)) {
        return USER_TLS_ERR_CRYPTO;
    }
    return 0;
}

static USER_CODE int user_tls_send_client_finished(user_tls_client_t* client) {
    uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t verify_data[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t raw[4U + USER_TLS_SHA256_DIGEST_SIZE];
    int status;

    if (!client) return NET_ERR_INVALID;
    user_tls_transcript_hash_snapshot(&client->transcript, transcript_hash);
    if (user_tls_compute_finished_verify_data(client->keys.client_finished_key,
                                              transcript_hash, verify_data) != 0) {
        return USER_TLS_ERR_CRYPTO;
    }

    raw[0] = USER_TLS_HANDSHAKE_FINISHED;
    user_tls_store_be24(raw + 1U, USER_TLS_SHA256_DIGEST_SIZE);
    memcpy(raw + 4U, verify_data, sizeof(verify_data));

    status = user_tls_send_protected_record(client, USER_TLS_CONTENT_HANDSHAKE, raw, sizeof(raw));
    if (status != 0) return status;
    user_tls_transcript_update(&client->transcript, raw, sizeof(raw));
    client->write_use_application_keys = 1U;
    client->application_write_seq = 0U;
    client->handshake_complete = 1U;
    return 0;
}

static USER_CODE int user_tls_send_close_notify(user_tls_client_t* client) {
    static const uint8_t close_notify_alert[2] = { 0x01U, 0x00U };
    int status;

    if (!client || !client->handshake_complete || !client->write_has_keys || client->close_notify_sent) return 0;
    status = user_tls_send_protected_record(client, USER_TLS_CONTENT_ALERT,
                                            close_notify_alert, sizeof(close_notify_alert));
    if (status == 0) client->close_notify_sent = 1U;
    return status;
}

static USER_CODE int user_tls_send_dummy_ccs(user_tls_client_t* client) {
    static const uint8_t payload[1] = { 0x01U };

    return user_tls_send_plain_record(client, USER_TLS_CONTENT_CHANGE_CIPHER_SPEC,
                                      USER_TLS_VERSION_TLS12, payload, sizeof(payload));
}

static USER_CODE int user_tls_fill_application_buffer(user_tls_client_t* client) {
    if (!client) return NET_ERR_INVALID;
    client->message_buf_len = 0U;
    client->message_buf_off = 0U;

    while (1) {
        uint8_t content_type = 0U;
        const uint8_t* payload = (const uint8_t*)0;
        uint16_t payload_len = 0U;
        int status = user_tls_read_record_content(client, &content_type, &payload, &payload_len);

        if (status == USER_TLS_INTERNAL_SKIP) continue;
        if (status < 0) return status;

        if (content_type == USER_TLS_CONTENT_APPLICATION_DATA) {
            if (payload_len == 0U) continue;
            memcpy(client->message_buf, payload, payload_len);
            client->message_buf_len = payload_len;
            client->message_buf_off = 0U;
            return 0;
        }

        if (content_type != USER_TLS_CONTENT_HANDSHAKE) return USER_TLS_ERR_PROTOCOL;
        if (client->message_buf_len + payload_len > USER_TLS_HANDSHAKE_BUFFER_SIZE) {
            return USER_TLS_ERR_RECORD_OVERFLOW;
        }
        memcpy(client->message_buf + client->message_buf_len, payload, payload_len);
        client->message_buf_len += payload_len;

        while (client->message_buf_len >= 4U) {
            uint32_t msg_len = user_tls_load_be24(client->message_buf + 1U);
            uint32_t total_len = 4U + msg_len;

            if (total_len > client->message_buf_len) break;
            if (client->message_buf[0] != USER_TLS_HANDSHAKE_NEW_SESSION_TICKET) {
                return USER_TLS_ERR_PROTOCOL;
            }
            user_tls_buffer_drop_prefix(client->message_buf, &client->message_buf_len, total_len);
        }

        if (client->message_buf_len == 0U) continue;
    }
}

static USER_CODE int user_tls_run_handshake(user_tls_client_t* client) {
    const uint8_t* raw = (const uint8_t*)0;
    const uint8_t* body = (const uint8_t*)0;
    uint32_t raw_len = 0U;
    uint32_t body_len = 0U;
    uint8_t message_type = 0U;
    uint8_t shared_secret[USER_TLS_X25519_KEY_SIZE];
    uint8_t transcript_hash[USER_TLS_SHA256_DIGEST_SIZE];
    int status;

    if (!client) return NET_ERR_INVALID;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_HANDSHAKE, "start");
    client->message_buf_len = 0U;
    client->message_buf_off = 0U;
    user_tls_transcript_init(&client->transcript);

    status = user_getrandom(client->client_random, sizeof(client->client_random));
    if (status != 0) return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_HANDSHAKE, USER_TLS_ERR_CRYPTO,
                                                "rng client_random");
    status = user_getrandom(client->session_id, sizeof(client->session_id));
    if (status != 0) return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_HANDSHAKE, USER_TLS_ERR_CRYPTO,
                                                "rng session_id");
    status = user_getrandom(client->client_private, sizeof(client->client_private));
    if (status != 0) return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_HANDSHAKE, USER_TLS_ERR_CRYPTO,
                                                "rng client_private");
    if (user_tls_x25519_public_key(client->client_public, client->client_private) != 0) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_HANDSHAKE, USER_TLS_ERR_CRYPTO,
                                   "x25519 public");
    }

    status = user_tls_build_client_hello(client->message_buf, USER_TLS_HANDSHAKE_BUFFER_SIZE, &raw_len,
                                         client->server_name, client->client_random,
                                         client->session_id, client->client_public);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_HANDSHAKE, status,
                                                     "build client_hello");
    user_tls_transcript_update(&client->transcript, client->message_buf, raw_len);
    status = user_tls_send_plain_record(client, USER_TLS_CONTENT_HANDSHAKE,
                                        USER_TLS_RECORD_VERSION_CLIENT_HELLO,
                                        client->message_buf, (uint16_t)raw_len);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_HANDSHAKE, status,
                                                     "send client_hello");
    status = user_tls_send_dummy_ccs(client);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_HANDSHAKE, status,
                                                     "send dummy_ccs");
    client->message_buf_len = 0U;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_SERVER_HELLO, "recv server_hello");
    status = user_tls_next_handshake_message(client, &raw, &raw_len, &message_type, &body, &body_len);
    if (status != 0) {
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_SERVER_HELLO, status, "recv server_hello");
    }
    if (message_type != USER_TLS_HANDSHAKE_SERVER_HELLO) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_SERVER_HELLO, USER_TLS_ERR_PROTOCOL,
                                   "unexpected server_hello");
    }
    (void)body;
    (void)body_len;
    status = user_tls_parse_server_hello(raw, raw_len, client->session_id,
                                         client->server_random, client->server_public);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_SERVER_HELLO, status,
                                                     "parse server_hello");
    user_tls_transcript_update(&client->transcript, raw, raw_len);
    user_tls_buffer_drop_prefix(client->message_buf, &client->message_buf_len, raw_len);

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_HANDSHAKE_KEYS, "derive handshake keys");
    if (user_tls_x25519(shared_secret, client->client_private, client->server_public) != 0 ||
        user_tls_is_all_zero(shared_secret, sizeof(shared_secret))) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_HANDSHAKE_KEYS, USER_TLS_ERR_CRYPTO,
                                   "x25519 shared_secret");
    }
    user_tls_transcript_hash_snapshot(&client->transcript, transcript_hash);
    status = user_tls_derive_handshake_schedule(&client->keys, shared_secret, transcript_hash);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_HANDSHAKE_KEYS, status,
                                                     "derive handshake keys");
    client->read_has_keys = 1U;
    client->write_has_keys = 1U;
    client->read_use_application_keys = 0U;
    client->write_use_application_keys = 0U;
    client->handshake_read_seq = 0U;
    client->handshake_write_seq = 0U;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_ENCRYPTED_EXTENSIONS, "recv encrypted_extensions");
    status = user_tls_next_handshake_message(client, &raw, &raw_len, &message_type, &body, &body_len);
    if (status != 0) {
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_ENCRYPTED_EXTENSIONS, status,
                                        "recv encrypted_extensions");
    }
    if (message_type != USER_TLS_HANDSHAKE_ENCRYPTED_EXTENSIONS) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_ENCRYPTED_EXTENSIONS, USER_TLS_ERR_PROTOCOL,
                                   "unexpected encrypted_extensions");
    }
    status = user_tls_parse_encrypted_extensions(raw, raw_len);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_ENCRYPTED_EXTENSIONS, status,
                                                     "parse encrypted_extensions");
    user_tls_transcript_update(&client->transcript, raw, raw_len);
    user_tls_buffer_drop_prefix(client->message_buf, &client->message_buf_len, raw_len);

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_CERTIFICATE, "recv certificate");
    status = user_tls_next_handshake_message(client, &raw, &raw_len, &message_type, &body, &body_len);
    if (status != 0) {
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_CERTIFICATE, status, "recv certificate");
    }
    if (message_type != USER_TLS_HANDSHAKE_CERTIFICATE) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_CERTIFICATE, USER_TLS_ERR_PROTOCOL,
                                   "unexpected certificate");
    }
    status = user_tls_process_certificate(client, raw, raw_len);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_CERTIFICATE, status,
                                                     "parse certificate");
    user_tls_transcript_update(&client->transcript, raw, raw_len);
    user_tls_buffer_drop_prefix(client->message_buf, &client->message_buf_len, raw_len);

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_CERTIFICATE_VERIFY, "recv certificate_verify");
    status = user_tls_next_handshake_message(client, &raw, &raw_len, &message_type, &body, &body_len);
    if (status != 0) {
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_CERTIFICATE_VERIFY, status,
                                        "recv certificate_verify");
    }
    if (message_type != USER_TLS_HANDSHAKE_CERTIFICATE_VERIFY) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_CERTIFICATE_VERIFY, USER_TLS_ERR_PROTOCOL,
                                   "unexpected certificate_verify");
    }
    status = user_tls_process_certificate_verify(client, raw, raw_len);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_CERTIFICATE_VERIFY, status,
                                                     "verify certificate");
    user_tls_transcript_update(&client->transcript, raw, raw_len);
    user_tls_buffer_drop_prefix(client->message_buf, &client->message_buf_len, raw_len);

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_SERVER_FINISHED, "recv server_finished");
    status = user_tls_next_handshake_message(client, &raw, &raw_len, &message_type, &body, &body_len);
    if (status != 0) {
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_SERVER_FINISHED, status,
                                        "recv server_finished");
    }
    if (message_type != USER_TLS_HANDSHAKE_FINISHED) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_SERVER_FINISHED, USER_TLS_ERR_PROTOCOL,
                                   "unexpected server_finished");
    }
    status = user_tls_process_server_finished(client, raw, raw_len);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_SERVER_FINISHED, status,
                                                     "verify server_finished");
    user_tls_transcript_update(&client->transcript, raw, raw_len);
    user_tls_buffer_drop_prefix(client->message_buf, &client->message_buf_len, raw_len);

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_CLIENT_FINISHED, "derive application keys");
    user_tls_transcript_hash_snapshot(&client->transcript, transcript_hash);
    status = user_tls_derive_application_schedule(&client->keys, transcript_hash);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_CLIENT_FINISHED, status,
                                                     "derive application keys");
    client->read_use_application_keys = 1U;
    client->application_read_seq = 0U;
    client->application_write_seq = 0U;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_CLIENT_FINISHED, "send client_finished");
    status = user_tls_send_client_finished(client);
    if (status != 0) return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_CLIENT_FINISHED, status,
                                                     "send client_finished");
    return 0;
}

const char* USER_CODE user_tls_default_ntp_host(void) {
    return user_tls_default_ntp_host_name;
}

int USER_CODE user_tls_get_utc_unix_time_for_host(const char* ntp_host, uint32_t* out_unix_seconds) {
    const char* host = ntp_host;

    if (!out_unix_seconds) return NET_ERR_INVALID;
    if (!host || host[0] == '\0') host = user_tls_default_ntp_host();
    return user_net_ntp_query(host, out_unix_seconds);
}

int USER_CODE user_tls_get_utc_unix_time(uint32_t* out_unix_seconds) {
    return user_tls_get_utc_unix_time_for_host((const char*)0, out_unix_seconds);
}

int USER_CODE user_tls_check_certificate_time(const user_tls_x509_cert_t* cert, uint32_t* out_unix_seconds) {
    uint32_t unix_seconds = 0;

    if (!cert) return USER_TLS_CERT_TIME_ERR_INVALID;
    if (user_tls_get_utc_unix_time(&unix_seconds) != NET_OK) return USER_TLS_CERT_TIME_ERR_NTP;
    if (out_unix_seconds) *out_unix_seconds = unix_seconds;
    if (user_tls_x509_is_valid_at(cert, unix_seconds) != 0) return USER_TLS_CERT_TIME_ERR_VALIDITY;
    return USER_TLS_CERT_TIME_OK;
}

int USER_CODE user_tls_open(const char* host) {
    user_tls_client_t* client;
    uint32_t unix_seconds = 0U;
    int status;

    user_tls_debug_reset();
    if (!host || host[0] == '\0') {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_OPEN, NET_ERR_INVALID, "invalid host");
    }
    user_tls_debug_note(USER_TLS_DEBUG_STAGE_OPEN, "pin lookup");
    if (user_tls_pins_lookup(host, (const user_tls_pin_entry_t**)0) != 0) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_OPEN, USER_TLS_ERR_PIN, "host not pinned");
    }
    user_tls_debug_note(USER_TLS_DEBUG_STAGE_OPEN, "ntp time");
    if (user_tls_get_utc_unix_time(&unix_seconds) != NET_OK) {
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_OPEN, USER_TLS_CERT_TIME_ERR_NTP, "ntp time");
    }

    (void)user_tls_close();
    status = user_tls_allocate_client();
    if (status != 0) return status;
    client = user_tls_client_state;
    user_tls_reset_client_runtime(client);
    status = user_tls_copy_host(client->server_name, sizeof(client->server_name), host);
    if (status != 0) {
        user_tls_free_client();
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_OPEN, status, "copy host");
    }
    client->validated_unix_time = unix_seconds;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_OPEN, "dns resolve");
    status = user_net_resolve(client->server_name, &client->peer_ip);
    if (status != NET_OK) {
        user_tls_free_client();
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_OPEN, NET_ERR_RESOLVE, "dns resolve");
    }
    if (client->peer_ip == 0U) {
        user_tls_free_client();
        return user_tls_debug_fail(USER_TLS_DEBUG_STAGE_OPEN, NET_ERR_RESOLVE, "dns resolve");
    }
    user_tls_debug_note(USER_TLS_DEBUG_STAGE_OPEN, "socket create");
    client->socket_handle = user_net_socket(NET_SOCK_STREAM);
    if (client->socket_handle < 0) {
        status = client->socket_handle;
        user_tls_free_client();
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_OPEN, status, "socket create");
    }
    user_tls_debug_note(USER_TLS_DEBUG_STAGE_OPEN, "tcp connect");
    status = user_net_connect(client->socket_handle, client->peer_ip,
                              USER_TLS_SOCKET_PORT, NET_TIMEOUT_CONNECT_DEFAULT);
    if (status != NET_OK) {
        (void)user_net_close(client->socket_handle);
        client->socket_handle = -1;
        user_tls_free_client();
        return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_OPEN, status, "tcp connect");
    }

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_HANDSHAKE, "run handshake");
    status = user_tls_run_handshake(client);
    if (status != 0) {
        client->last_error = status;
        (void)user_tls_close();
        return status;
    }
    return NET_OK;
}

int USER_CODE user_tls_send(const void* data, uint32_t len) {
    user_tls_client_t* client = user_tls_client_state;
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sent = 0U;

    if (!client || !client->handshake_complete) return USER_TLS_ERR_NOT_CONNECTED;
    if (!data && len != 0U) return NET_ERR_INVALID;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_APPDATA_SEND, "send application data");
    while (sent < len) {
        uint32_t chunk = len - sent;
        int status;

        if (chunk > USER_TLS_RECORD_PLAINTEXT_MAX) chunk = USER_TLS_RECORD_PLAINTEXT_MAX;
        status = user_tls_send_protected_record(client, USER_TLS_CONTENT_APPLICATION_DATA,
                                                bytes + sent, chunk);
        if (status != 0) {
            return sent != 0U ? (int)sent :
                   user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_APPDATA_SEND, status,
                                            "send application data");
        }
        sent += chunk;
    }
    return (int)sent;
}

int USER_CODE user_tls_recv(void* data, uint32_t len) {
    user_tls_client_t* client = user_tls_client_state;
    uint32_t available;

    if (!client || !client->handshake_complete) return USER_TLS_ERR_NOT_CONNECTED;
    if (!data && len != 0U) return NET_ERR_INVALID;
    if (len == 0U) return 0;

    user_tls_debug_note(USER_TLS_DEBUG_STAGE_APPDATA_RECV, "recv application data");
    available = client->message_buf_len > client->message_buf_off ?
                client->message_buf_len - client->message_buf_off : 0U;
    if (available == 0U) {
        int status = user_tls_fill_application_buffer(client);
        if (status < 0) {
            return user_tls_debug_propagate(USER_TLS_DEBUG_STAGE_APPDATA_RECV, status,
                                            "recv application data");
        }
        available = client->message_buf_len - client->message_buf_off;
    }

    if (available > len) available = len;
    memcpy(data, client->message_buf + client->message_buf_off, available);
    client->message_buf_off += available;
    if (client->message_buf_off >= client->message_buf_len) {
        client->message_buf_len = 0U;
        client->message_buf_off = 0U;
    }
    return (int)available;
}

int USER_CODE user_tls_close(void) {
    user_tls_client_t* client = user_tls_client_state;
    int status = 0;

    if (user_tls_debug_status_code == 0) user_tls_debug_note(USER_TLS_DEBUG_STAGE_CLOSE, "close");
    if (!client) return 0;
    if (client->socket_handle >= 0) {
        if (client->handshake_complete && !client->peer_closed) {
            int alert_status = user_tls_send_close_notify(client);
            if (status == 0 && alert_status < 0) status = alert_status;
        }
        {
            int close_status = user_net_close(client->socket_handle);
            if (status == 0 && close_status < 0 &&
                close_status != NET_ERR_CLOSED && close_status != NET_ERR_TIMEOUT) {
                status = close_status;
            }
        }
    }
    user_tls_free_client();
    return status;
}

const char* USER_CODE user_tls_error_string(int status) {
    switch (status) {
        case NET_OK: return user_tls_detail_dispatch_ok;
        case NET_ERR_INVALID: return user_tls_error_invalid;
        case NET_ERR_UNSUPPORTED: return user_tls_error_unsupported;
        case NET_ERR_NOT_READY: return user_tls_error_network_not_ready;
        case NET_ERR_NO_SOCKETS: return user_tls_error_no_sockets;
        case NET_ERR_RESOLVE: return user_tls_error_dns_resolve;
        case NET_ERR_TIMEOUT: return user_tls_error_timeout;
        case NET_ERR_STATE: return user_tls_error_socket_state;
        case NET_ERR_RESET: return user_tls_error_reset;
        case NET_ERR_CLOSED: return user_tls_error_closed;
        case NET_ERR_WOULD_BLOCK: return user_tls_error_would_block;
        case NET_ERR_IO: return user_tls_error_io;
        case NET_ERR_OVERFLOW: return user_tls_error_buffer_overflow;
        case USER_TLS_CERT_TIME_ERR_INVALID: return user_tls_error_invalid;
        case USER_TLS_CERT_TIME_ERR_NTP: return user_tls_error_ntp;
        case USER_TLS_CERT_TIME_ERR_VALIDITY: return user_tls_error_cert_time;
        case USER_TLS_ERR_ALLOC: return user_tls_error_alloc;
        case USER_TLS_ERR_PROTOCOL: return user_tls_error_protocol;
        case USER_TLS_ERR_CRYPTO: return user_tls_error_crypto;
        case USER_TLS_ERR_PIN: return user_tls_error_pin;
        case USER_TLS_ERR_HOSTNAME: return user_tls_error_hostname;
        case USER_TLS_ERR_CERTIFICATE: return user_tls_error_certificate;
        case USER_TLS_ERR_CERT_TIME: return user_tls_error_cert_time;
        case USER_TLS_ERR_NOT_CONNECTED: return user_tls_error_not_connected;
        case USER_TLS_ERR_RECORD_OVERFLOW: return user_tls_error_record_overflow;
        case USER_TLS_ERR_ALERT: return user_tls_error_alert;
        default: return user_tls_error_unknown;
    }
}

const char* USER_CODE user_tls_debug_stage_name(void) {
    switch (user_tls_debug_stage) {
        case USER_TLS_DEBUG_STAGE_OPEN: return "open";
        case USER_TLS_DEBUG_STAGE_HANDSHAKE: return "handshake";
        case USER_TLS_DEBUG_STAGE_SERVER_HELLO: return "server_hello";
        case USER_TLS_DEBUG_STAGE_HANDSHAKE_KEYS: return "handshake_keys";
        case USER_TLS_DEBUG_STAGE_ENCRYPTED_EXTENSIONS: return "encrypted_extensions";
        case USER_TLS_DEBUG_STAGE_CERTIFICATE: return "certificate";
        case USER_TLS_DEBUG_STAGE_CERTIFICATE_VERIFY: return "certificate_verify";
        case USER_TLS_DEBUG_STAGE_SERVER_FINISHED: return "server_finished";
        case USER_TLS_DEBUG_STAGE_CLIENT_FINISHED: return "client_finished";
        case USER_TLS_DEBUG_STAGE_APPDATA_SEND: return "appdata_send";
        case USER_TLS_DEBUG_STAGE_APPDATA_RECV: return "appdata_recv";
        case USER_TLS_DEBUG_STAGE_RECORD_ENCRYPT: return "record_encrypt";
        case USER_TLS_DEBUG_STAGE_RECORD_DECRYPT: return "record_decrypt";
        case USER_TLS_DEBUG_STAGE_CLOSE: return "close";
        default: return "idle";
    }
}

const char* USER_CODE user_tls_debug_detail(void) {
    return user_tls_debug_detail_storage;
}

int USER_CODE user_tls_debug_status(void) {
    return user_tls_debug_status_code;
}

static USER_CODE int user_tls_selftest_tls13_vectors(char* detail, uint32_t detail_len) {
    uint8_t client_random[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t session_id[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t client_public[USER_TLS_X25519_KEY_SIZE];
    uint8_t raw[512];
    uint8_t server_random[USER_TLS_SHA256_DIGEST_SIZE];
    uint8_t server_public[USER_TLS_X25519_KEY_SIZE];
    uint8_t finished[USER_TLS_SHA256_DIGEST_SIZE];
    user_tls_key_schedule_t schedule;
    user_tls_cursor_t cursor;
    const uint8_t* ext_bytes = (const uint8_t*)0;
    const uint8_t* ext_data = (const uint8_t*)0;
    uint32_t raw_len = 0U;
    uint16_t ext_len = 0U;
    uint16_t version = 0U;
    uint8_t sid_len = 0U;
    const uint8_t* random_ptr = (const uint8_t*)0;
    const uint8_t* sid_ptr = (const uint8_t*)0;
    uint16_t cipher_len = 0U;
    uint16_t cipher = 0U;
    uint8_t compression_len = 0U;
    uint8_t compression = 0U;
    uint16_t total_ext_len = 0U;

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';

    user_tls_fill_sequence(client_random, sizeof(client_random), 0x00U);
    user_tls_fill_sequence(session_id, sizeof(session_id), 0x20U);
    user_tls_fill_sequence(client_public, sizeof(client_public), 0x40U);

    if (user_tls_build_client_hello(raw, sizeof(raw), &raw_len,
                                    "test.example.com",
                                    client_random, session_id, client_public) != 0 ||
        raw[0] != USER_TLS_HANDSHAKE_CLIENT_HELLO ||
        user_tls_load_be24(raw + 1U) + 4U != raw_len) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_tls13_client_hello_fail);
        return -1;
    }

    cursor.data = raw + 4U;
    cursor.len = raw_len - 4U;
    cursor.off = 0U;
    if (user_tls_cursor_read_u16(&cursor, &version) != 0 ||
        version != USER_TLS_VERSION_TLS12 ||
        user_tls_cursor_read_bytes(&cursor, &random_ptr, USER_TLS_SHA256_DIGEST_SIZE) != 0 ||
        memcmp(random_ptr, client_random, sizeof(client_random)) != 0 ||
        user_tls_cursor_read_u8(&cursor, &sid_len) != 0 ||
        sid_len != USER_TLS_SHA256_DIGEST_SIZE ||
        user_tls_cursor_read_bytes(&cursor, &sid_ptr, sid_len) != 0 ||
        memcmp(sid_ptr, session_id, sizeof(session_id)) != 0 ||
        user_tls_cursor_read_u16(&cursor, &cipher_len) != 0 ||
        cipher_len != 2U ||
        user_tls_cursor_read_u16(&cursor, &cipher) != 0 ||
        cipher != USER_TLS_CIPHER_TLS_AES_128_GCM_SHA256 ||
        user_tls_cursor_read_u8(&cursor, &compression_len) != 0 ||
        compression_len != 1U ||
        user_tls_cursor_read_u8(&cursor, &compression) != 0 ||
        compression != 0U ||
        user_tls_cursor_read_u16(&cursor, &total_ext_len) != 0 ||
        cursor.off + total_ext_len != cursor.len) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_tls13_client_hello_fail);
        return -1;
    }
    ext_bytes = cursor.data + cursor.off;
    ext_len = total_ext_len;

    if (user_tls_find_extension(ext_bytes, ext_len, USER_TLS_EXT_SUPPORTED_VERSIONS, &ext_data, &ext_len) != 0 ||
        ext_len != 3U || ext_data[0] != 2U || user_tls_load_be16(ext_data + 1U) != USER_TLS_VERSION_TLS13 ||
        user_tls_find_extension(ext_bytes, total_ext_len, USER_TLS_EXT_KEY_SHARE, &ext_data, &ext_len) != 0 ||
        ext_len != 38U || user_tls_load_be16(ext_data) != 36U ||
        user_tls_load_be16(ext_data + 2U) != USER_TLS_GROUP_X25519 ||
        user_tls_load_be16(ext_data + 4U) != USER_TLS_X25519_KEY_SIZE ||
        memcmp(ext_data + 6U, client_public, sizeof(client_public)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_tls13_client_hello_fail);
        return -1;
    }

    if (user_tls_parse_server_hello(user_tls_tls13_selftest_server_hello,
                                    sizeof(user_tls_tls13_selftest_server_hello),
                                    session_id, server_random, server_public) != 0 ||
        server_random[0] != 0x60U || server_random[31] != 0x7fU ||
        server_public[0] != 0x40U || server_public[31] != 0x5fU) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_tls13_server_hello_fail);
        return -1;
    }

    if (user_tls_derive_handshake_schedule(&schedule,
                                           user_tls_tls13_selftest_shared_secret,
                                           user_tls_tls13_selftest_transcript_hash) != 0 ||
        user_tls_derive_application_schedule(&schedule,
                                             user_tls_tls13_selftest_transcript_hash) != 0 ||
        memcmp(schedule.client_handshake_key, user_tls_tls13_selftest_client_hs_key,
               sizeof(schedule.client_handshake_key)) != 0 ||
        memcmp(schedule.server_handshake_key, user_tls_tls13_selftest_server_hs_key,
               sizeof(schedule.server_handshake_key)) != 0 ||
        memcmp(schedule.client_handshake_iv, user_tls_tls13_selftest_client_hs_iv,
               sizeof(schedule.client_handshake_iv)) != 0 ||
        memcmp(schedule.server_handshake_iv, user_tls_tls13_selftest_server_hs_iv,
               sizeof(schedule.server_handshake_iv)) != 0 ||
        memcmp(schedule.client_application_key, user_tls_tls13_selftest_client_app_key,
               sizeof(schedule.client_application_key)) != 0 ||
        memcmp(schedule.server_application_key, user_tls_tls13_selftest_server_app_key,
               sizeof(schedule.server_application_key)) != 0 ||
        memcmp(schedule.client_application_iv, user_tls_tls13_selftest_client_app_iv,
               sizeof(schedule.client_application_iv)) != 0 ||
        memcmp(schedule.server_application_iv, user_tls_tls13_selftest_server_app_iv,
               sizeof(schedule.server_application_iv)) != 0 ||
        user_tls_compute_finished_verify_data(schedule.server_finished_key,
                                              user_tls_tls13_selftest_transcript_hash, finished) != 0 ||
        memcmp(finished, user_tls_tls13_selftest_server_finished, sizeof(finished)) != 0 ||
        user_tls_compute_finished_verify_data(schedule.client_finished_key,
                                              user_tls_tls13_selftest_transcript_hash, finished) != 0 ||
        memcmp(finished, user_tls_tls13_selftest_client_finished, sizeof(finished)) != 0) {
        user_tls_copy_detail(detail, detail_len, user_tls_selftest_tls13_schedule_fail);
        return -1;
    }

    user_tls_copy_detail(detail, detail_len, user_tls_selftest_tls13_ok);
    return 0;
}

static USER_CODE void user_tls_bump_status_counter(user_tls_selftest_report_t* report, uint32_t status) {
    if (!report) return;

    if (status == USER_TLS_TEST_STATUS_PASS) report->pass_count++;
    else if (status == USER_TLS_TEST_STATUS_FAIL) report->fail_count++;
    else if (status == USER_TLS_TEST_STATUS_PENDING) report->pending_count++;
    else if (status == USER_TLS_TEST_STATUS_SKIP) report->skip_count++;
}

static USER_CODE void user_tls_record_result(user_tls_selftest_report_t* report,
                                             const char* name, uint32_t status, const char* detail) {
    uint32_t index;

    if (!report) return;
    index = report->total_count;
    report->total_count++;
    user_tls_bump_status_counter(report, status);
    if (index >= USER_TLS_SELFTEST_MAX_CASES) return;

    report->cases[index].status = status;
    user_tls_copy_text(report->cases[index].name, USER_TLS_SELFTEST_NAME_LEN, name);
    user_tls_copy_text(report->cases[index].detail, USER_TLS_SELFTEST_DETAIL_LEN, detail);
}

static USER_CODE void user_tls_selftest_framework(user_tls_selftest_report_t* report) {
    user_tls_record_result(report, user_tls_name_framework, USER_TLS_TEST_STATUS_PASS,
                           user_tls_detail_dispatch_ok);
}

static USER_CODE void user_tls_selftest_hash_kdf(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_crypto_selftest_hash_kdf(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_hash_kdf, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_hash_kdf, USER_TLS_TEST_STATUS_FAIL, detail);
}

static USER_CODE void user_tls_selftest_aes_gcm(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_crypto_selftest_aes_gcm(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_aes_gcm, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_aes_gcm, USER_TLS_TEST_STATUS_FAIL, detail);
}

static USER_CODE void user_tls_selftest_x25519(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_crypto_selftest_x25519(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_x25519, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_x25519, USER_TLS_TEST_STATUS_FAIL, detail);
}

static USER_CODE void user_tls_selftest_rsa_pss(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_crypto_selftest_rsa_pss(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_rsa_pss, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_rsa_pss, USER_TLS_TEST_STATUS_FAIL, detail);
}

static USER_CODE void user_tls_selftest_ecdsa_p256(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_crypto_selftest_ecdsa_p256(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_ecdsa_p256, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_ecdsa_p256, USER_TLS_TEST_STATUS_FAIL, detail);
}

static USER_CODE void user_tls_selftest_x509(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_x509_selftest(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_x509, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_x509, USER_TLS_TEST_STATUS_FAIL, detail);
}

static USER_CODE void user_tls_selftest_tls13(user_tls_selftest_report_t* report) {
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];

    if (!report) return;
    if (user_tls_selftest_tls13_vectors(detail, sizeof(detail)) == 0) {
        user_tls_record_result(report, user_tls_name_tls13, USER_TLS_TEST_STATUS_PASS, detail);
        return;
    }
    user_tls_record_result(report, user_tls_name_tls13, USER_TLS_TEST_STATUS_FAIL, detail);
}

int USER_CODE user_tls_run_selftests(user_tls_selftest_report_t* out_report) {
    if (!out_report) return -1;

    memset(out_report, 0, sizeof(*out_report));
    user_tls_selftest_framework(out_report);
    user_tls_selftest_hash_kdf(out_report);
    user_tls_selftest_aes_gcm(out_report);
    user_tls_selftest_x25519(out_report);
    user_tls_selftest_rsa_pss(out_report);
    user_tls_selftest_ecdsa_p256(out_report);
    user_tls_selftest_x509(out_report);
    user_tls_selftest_tls13(out_report);
    return out_report->fail_count == 0U ? 0 : -1;
}

const char* USER_CODE user_tls_test_status_name(uint32_t status) {
    if (status == USER_TLS_TEST_STATUS_PASS) return user_tls_status_pass;
    if (status == USER_TLS_TEST_STATUS_FAIL) return user_tls_status_fail;
    if (status == USER_TLS_TEST_STATUS_PENDING) return user_tls_status_pending;
    if (status == USER_TLS_TEST_STATUS_SKIP) return user_tls_status_skip;
    return user_tls_status_unknown;
}
