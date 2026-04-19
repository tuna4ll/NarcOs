#ifndef USER_TLS_H
#define USER_TLS_H

#include <stdint.h>
#include "user_tls_x509.h"

#define USER_TLS_SELFTEST_MAX_CASES 8U
#define USER_TLS_SELFTEST_NAME_LEN 24U
#define USER_TLS_SELFTEST_DETAIL_LEN 96U
#define USER_TLS_MAX_HOSTNAME_LEN 95U

typedef enum {
    USER_TLS_TEST_STATUS_PASS = 0,
    USER_TLS_TEST_STATUS_FAIL = 1,
    USER_TLS_TEST_STATUS_PENDING = 2,
    USER_TLS_TEST_STATUS_SKIP = 3
} user_tls_test_status_t;

typedef struct {
    uint32_t status;
    char name[USER_TLS_SELFTEST_NAME_LEN];
    char detail[USER_TLS_SELFTEST_DETAIL_LEN];
} user_tls_selftest_case_t;

typedef struct {
    uint32_t total_count;
    uint32_t pass_count;
    uint32_t fail_count;
    uint32_t pending_count;
    uint32_t skip_count;
    user_tls_selftest_case_t cases[USER_TLS_SELFTEST_MAX_CASES];
} user_tls_selftest_report_t;

typedef enum {
    USER_TLS_CERT_TIME_OK = 0,
    USER_TLS_CERT_TIME_ERR_INVALID = -20,
    USER_TLS_CERT_TIME_ERR_NTP = -21,
    USER_TLS_CERT_TIME_ERR_VALIDITY = -22
} user_tls_cert_time_status_t;

typedef enum {
    USER_TLS_ERR_ALLOC = -100,
    USER_TLS_ERR_PROTOCOL = -101,
    USER_TLS_ERR_CRYPTO = -102,
    USER_TLS_ERR_PIN = -103,
    USER_TLS_ERR_HOSTNAME = -104,
    USER_TLS_ERR_CERTIFICATE = -105,
    USER_TLS_ERR_CERT_TIME = -106,
    USER_TLS_ERR_NOT_CONNECTED = -107,
    USER_TLS_ERR_RECORD_OVERFLOW = -108,
    USER_TLS_ERR_ALERT = -109
} user_tls_status_t;

const char* user_tls_default_ntp_host(void);
int user_tls_get_utc_unix_time_for_host(const char* ntp_host, uint32_t* out_unix_seconds);
int user_tls_get_utc_unix_time(uint32_t* out_unix_seconds);
int user_tls_check_certificate_time(const user_tls_x509_cert_t* cert, uint32_t* out_unix_seconds);
int user_tls_open(const char* host);
int user_tls_send(const void* data, uint32_t len);
int user_tls_recv(void* data, uint32_t len);
int user_tls_close(void);
const char* user_tls_error_string(int status);
const char* user_tls_debug_stage_name(void);
const char* user_tls_debug_detail(void);
int user_tls_debug_status(void);
int user_tls_run_selftests(user_tls_selftest_report_t* out_report);
const char* user_tls_test_status_name(uint32_t status);

#endif
