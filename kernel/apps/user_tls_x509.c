#include <stdint.h>
#include "user_tls_x509.h"
#include "user_tls_pins.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

#define memset user_memset
#define memcpy user_memcpy

typedef struct {
    const uint8_t* raw;
    uint32_t raw_len;
    const uint8_t* value;
    uint32_t len;
    uint8_t tag;
} user_tls_x509_tlv_t;

static const uint8_t user_tls_x509_oid_rsa_encryption[] USER_RODATA = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
};

static const uint8_t user_tls_x509_oid_subject_alt_name[] USER_RODATA = {
    0x55, 0x1d, 0x11
};

static const uint8_t user_tls_x509_test_cert_der[] USER_RODATA = {
    0x30, 0x82, 0x02, 0x66, 0x30, 0x82, 0x01, 0xcf, 0xa0, 0x03, 0x02, 0x01,
    0x02, 0x02, 0x02, 0x12, 0x34, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
    0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x30, 0x34, 0x31, 0x1c,
    0x30, 0x1a, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x13, 0x69, 0x67, 0x6e,
    0x6f, 0x72, 0x65, 0x64, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65,
    0x2e, 0x63, 0x6f, 0x6d, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03, 0x55, 0x04,
    0x0a, 0x0c, 0x0b, 0x4e, 0x61, 0x72, 0x63, 0x4f, 0x73, 0x20, 0x54, 0x65,
    0x73, 0x74, 0x30, 0x1e, 0x17, 0x0d, 0x32, 0x36, 0x30, 0x34, 0x31, 0x38,
    0x32, 0x30, 0x32, 0x37, 0x33, 0x37, 0x5a, 0x17, 0x0d, 0x32, 0x38, 0x30,
    0x37, 0x32, 0x31, 0x32, 0x30, 0x32, 0x37, 0x33, 0x37, 0x5a, 0x30, 0x34,
    0x31, 0x1c, 0x30, 0x1a, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x13, 0x69,
    0x67, 0x6e, 0x6f, 0x72, 0x65, 0x64, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70,
    0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x31, 0x14, 0x30, 0x12, 0x06, 0x03,
    0x55, 0x04, 0x0a, 0x0c, 0x0b, 0x4e, 0x61, 0x72, 0x63, 0x4f, 0x73, 0x20,
    0x54, 0x65, 0x73, 0x74, 0x30, 0x81, 0x9f, 0x30, 0x0d, 0x06, 0x09, 0x2a,
    0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x81,
    0x8d, 0x00, 0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00, 0x99, 0xef, 0xc5,
    0xbe, 0x2f, 0x5c, 0x89, 0x6b, 0x2a, 0x83, 0x87, 0x50, 0xaf, 0x09, 0x8d,
    0xba, 0x4f, 0x61, 0x0a, 0x65, 0x66, 0xc9, 0x5d, 0x7a, 0x0a, 0x4e, 0x88,
    0x5b, 0xc4, 0x8f, 0xfd, 0x0f, 0x49, 0xf9, 0x97, 0x3a, 0xf6, 0x2a, 0x7e,
    0x5e, 0x39, 0x2a, 0x87, 0xd8, 0x2a, 0xb5, 0xbe, 0x4b, 0xa9, 0xb7, 0x76,
    0x19, 0xcb, 0xd3, 0x8c, 0x34, 0x57, 0xd7, 0x43, 0x85, 0xe4, 0xe5, 0xff,
    0x64, 0x70, 0xa9, 0x3a, 0x01, 0x00, 0xdc, 0xbb, 0x00, 0x1e, 0x94, 0xfc,
    0x76, 0x4d, 0x25, 0x89, 0x1b, 0x4b, 0xfe, 0xb1, 0xd0, 0xc0, 0xae, 0x6b,
    0xe7, 0xe7, 0xef, 0xfe, 0x2e, 0xa5, 0x52, 0x92, 0x0b, 0xfc, 0xb5, 0x07,
    0xc0, 0xd0, 0x50, 0xc7, 0x1e, 0xe0, 0x34, 0x92, 0xa3, 0x37, 0xa6, 0x42,
    0xe5, 0x62, 0x04, 0xef, 0xa4, 0xb2, 0x1f, 0xea, 0x6f, 0xb2, 0x16, 0x42,
    0xa3, 0x13, 0x03, 0x79, 0xb7, 0x02, 0x03, 0x01, 0x00, 0x01, 0xa3, 0x81,
    0x86, 0x30, 0x81, 0x83, 0x30, 0x44, 0x06, 0x03, 0x55, 0x1d, 0x11, 0x04,
    0x3d, 0x30, 0x3b, 0x82, 0x10, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x65, 0x78,
    0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x82, 0x14, 0x61,
    0x70, 0x69, 0x2e, 0x74, 0x65, 0x73, 0x74, 0x2e, 0x65, 0x78, 0x61, 0x6d,
    0x70, 0x6c, 0x65, 0x2e, 0x63, 0x6f, 0x6d, 0x82, 0x11, 0x2a, 0x2e, 0x73,
    0x76, 0x63, 0x2e, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65, 0x2e, 0x63,
    0x6f, 0x6d, 0x30, 0x0c, 0x06, 0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff,
    0x04, 0x02, 0x30, 0x00, 0x30, 0x0e, 0x06, 0x03, 0x55, 0x1d, 0x0f, 0x01,
    0x01, 0xff, 0x04, 0x04, 0x03, 0x02, 0x05, 0xa0, 0x30, 0x1d, 0x06, 0x03,
    0x55, 0x1d, 0x0e, 0x04, 0x16, 0x04, 0x14, 0xf0, 0x16, 0x0b, 0x68, 0x75,
    0x51, 0xf5, 0xa4, 0xe5, 0xe6, 0x57, 0xf4, 0x55, 0x12, 0x57, 0xc0, 0xc9,
    0xe6, 0x32, 0x81, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7,
    0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x03, 0x81, 0x81, 0x00, 0x11, 0x8e,
    0xaa, 0xfd, 0xc3, 0xe3, 0x6d, 0x64, 0x9a, 0x1b, 0x89, 0x0c, 0x3c, 0xc1,
    0x33, 0x8b, 0x86, 0xc6, 0x96, 0x9b, 0x52, 0x0c, 0x26, 0x93, 0xb1, 0xa1,
    0x45, 0xbd, 0xa9, 0xb8, 0x3c, 0xcc, 0xfd, 0x80, 0xd6, 0x28, 0x21, 0x96,
    0x49, 0x42, 0x59, 0x4a, 0x01, 0x71, 0xfc, 0x81, 0x15, 0x51, 0xc7, 0x5a,
    0x7c, 0x74, 0x61, 0xe0, 0x1c, 0xd0, 0x0b, 0xa3, 0x5e, 0x6e, 0x32, 0x18,
    0x8d, 0xcb, 0x97, 0x80, 0xb0, 0x62, 0x13, 0x42, 0xa7, 0xf4, 0xf9, 0x9e,
    0x2e, 0x7d, 0x20, 0x41, 0x81, 0xb5, 0xba, 0x16, 0x58, 0xd6, 0xf0, 0xab,
    0x4f, 0x8f, 0xb1, 0xc3, 0xb3, 0xee, 0xe2, 0xea, 0x56, 0x52, 0x50, 0x01,
    0xd7, 0xcf, 0x2a, 0x8e, 0x35, 0x46, 0x29, 0xaa, 0x0f, 0xfb, 0xf8, 0x11,
    0x86, 0x0e, 0x7d, 0x98, 0xea, 0xcc, 0x15, 0x4b, 0xee, 0xcd, 0x90, 0x66,
    0x6e, 0x0c, 0x57, 0xc4, 0x95, 0xa5
};

static const uint8_t user_tls_x509_test_spki_hash[USER_TLS_SHA256_DIGEST_SIZE] USER_RODATA = {
    0x66, 0x0e, 0x21, 0xc0, 0x5c, 0x1d, 0xdb, 0x0b,
    0x67, 0x56, 0x51, 0xce, 0x3d, 0xbf, 0xb6, 0xc1,
    0x8d, 0xcf, 0x52, 0xd3, 0x3f, 0x3a, 0x04, 0x50,
    0x62, 0x91, 0x1a, 0xd0, 0xf3, 0x9a, 0x78, 0xfc
};

static const uint8_t user_tls_x509_generalized_time_tc[] USER_RODATA = {
    0x18, 0x0f, 0x32, 0x30, 0x35, 0x31, 0x30, 0x31, 0x30, 0x32,
    0x30, 0x33, 0x30, 0x34, 0x30, 0x35, 0x5a
};

static const uint8_t user_tls_x509_days_per_month[12] USER_RODATA = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static const char user_tls_x509_selftest_ok[] USER_RODATA = "x509+pins ok";
static const char user_tls_x509_selftest_parse_fail[] USER_RODATA = "leaf parse";
static const char user_tls_x509_selftest_time_fail[] USER_RODATA = "validity parse";
static const char user_tls_x509_selftest_time_tc_fail[] USER_RODATA = "generalized time";
static const char user_tls_x509_selftest_spki_fail[] USER_RODATA = "spki hash";
static const char user_tls_x509_selftest_hostname_fail[] USER_RODATA = "hostname match";
static const char user_tls_x509_selftest_validity_fail[] USER_RODATA = "validity check";
static const char user_tls_x509_selftest_rsa_fail[] USER_RODATA = "rsa key parse";
static const char user_tls_x509_selftest_reject_fail[] USER_RODATA = "reject malformed";

static USER_CODE void user_tls_x509_copy_detail(char* detail, uint32_t detail_len, const char* text) {
    uint32_t off = 0;

    if (!detail || detail_len == 0U) return;
    if (!text) text = "";
    while (text[off] != '\0' && off + 1U < detail_len) {
        detail[off] = text[off];
        off++;
    }
    detail[off] = '\0';
}

static USER_CODE int user_tls_x509_is_digit(uint8_t ch) {
    return ch >= '0' && ch <= '9';
}

static USER_CODE char user_tls_x509_ascii_lower(uint8_t ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch - 'A' + 'a');
    return (char)ch;
}

static USER_CODE int user_tls_x509_mem_equals(const uint8_t* a, uint32_t a_len,
                                              const uint8_t* b, uint32_t b_len) {
    if (!a || !b) return 0;
    if (a_len != b_len) return 0;
    return user_memcmp(a, b, a_len) == 0;
}

static USER_CODE int user_tls_x509_casecmp_tail(const char* host_tail, const uint8_t* pattern, uint32_t len) {
    if (!host_tail || !pattern) return 0;
    for (uint32_t i = 0; i < len; i++) {
        if (host_tail[i] == '\0') return 0;
        if (user_tls_x509_ascii_lower((uint8_t)host_tail[i]) != user_tls_x509_ascii_lower(pattern[i])) return 0;
    }
    return host_tail[len] == '\0';
}

static USER_CODE int user_tls_x509_casecmp_span_cstr(const user_tls_x509_span_t* span, const char* text) {
    uint32_t i = 0;

    if (!span || !text) return 0;
    while (i < span->len && text[i] != '\0') {
        if (user_tls_x509_ascii_lower(span->data[i]) != user_tls_x509_ascii_lower((uint8_t)text[i])) return 0;
        i++;
    }
    return i == span->len && text[i] == '\0';
}

static USER_CODE int user_tls_x509_is_leap_year(uint32_t year) {
    if ((year % 400U) == 0U) return 1;
    if ((year % 100U) == 0U) return 0;
    return (year % 4U) == 0U;
}

static USER_CODE uint32_t user_tls_x509_days_in_month(uint32_t year, uint32_t month) {
    if (month < 1U || month > 12U) return 0U;
    if (month == 2U && user_tls_x509_is_leap_year(year)) return 29U;
    return user_tls_x509_days_per_month[month - 1U];
}

static USER_CODE int user_tls_x509_date_to_unix(uint32_t year, uint32_t month, uint32_t day,
                                                uint32_t hour, uint32_t minute, uint32_t second,
                                                uint64_t* out_unix_time) {
    uint64_t days = 0;

    if (!out_unix_time) return -1;
    if (year < 1970U || month < 1U || month > 12U) return -1;
    if (day < 1U || day > user_tls_x509_days_in_month(year, month)) return -1;
    if (hour > 23U || minute > 59U || second > 59U) return -1;

    for (uint32_t y = 1970U; y < year; y++) {
        days += user_tls_x509_is_leap_year(y) ? 366U : 365U;
    }
    for (uint32_t m = 1U; m < month; m++) {
        days += user_tls_x509_days_in_month(year, m);
    }
    days += (uint64_t)(day - 1U);

    *out_unix_time = days * 86400ULL + (uint64_t)hour * 3600ULL +
                     (uint64_t)minute * 60ULL + (uint64_t)second;
    return 0;
}

static USER_CODE int user_tls_x509_parse_decimal(const uint8_t* text, uint32_t digits, uint32_t* out_value) {
    uint32_t value = 0;

    if (!text || !out_value || digits == 0U) return -1;
    for (uint32_t i = 0; i < digits; i++) {
        if (!user_tls_x509_is_digit(text[i])) return -1;
        value = value * 10U + (uint32_t)(text[i] - '0');
    }
    *out_value = value;
    return 0;
}

static USER_CODE int user_tls_x509_read_tlv(const uint8_t* buf, uint32_t buf_len,
                                            uint32_t* io_offset, user_tls_x509_tlv_t* out_tlv) {
    uint32_t start;
    uint32_t off;
    uint32_t len = 0;
    uint32_t len_octets = 0;

    if (!buf || !io_offset || !out_tlv) return -1;
    start = *io_offset;
    if (start >= buf_len || buf_len - start < 2U) return -1;

    off = start;
    out_tlv->tag = buf[off++];
    if ((buf[off] & 0x80U) == 0U) {
        len = buf[off++];
    } else {
        len_octets = (uint32_t)(buf[off++] & 0x7fU);
        if (len_octets == 0U || len_octets > 4U) return -1;
        if (off + len_octets > buf_len) return -1;
        if (buf[off] == 0U) return -1;
        for (uint32_t i = 0; i < len_octets; i++) {
            len = (len << 8) | buf[off++];
        }
        if (len < 128U) return -1;
    }

    if (off + len > buf_len) return -1;
    out_tlv->raw = buf + start;
    out_tlv->raw_len = off + len - start;
    out_tlv->value = buf + off;
    out_tlv->len = len;
    *io_offset = off + len;
    return 0;
}

static USER_CODE int user_tls_x509_parse_time_tlv(const user_tls_x509_tlv_t* tlv, uint64_t* out_unix_time) {
    uint32_t year = 0;
    uint32_t month = 0;
    uint32_t day = 0;
    uint32_t hour = 0;
    uint32_t minute = 0;
    uint32_t second = 0;

    if (!tlv || !out_unix_time) return -1;
    if (tlv->tag == 0x17U) {
        uint32_t short_year = 0;

        if (tlv->len != 13U || tlv->value[12] != 'Z') return -1;
        if (user_tls_x509_parse_decimal(tlv->value + 0U, 2U, &short_year) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 2U, 2U, &month) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 4U, 2U, &day) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 6U, 2U, &hour) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 8U, 2U, &minute) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 10U, 2U, &second) != 0) {
            return -1;
        }
        year = short_year >= 50U ? 1900U + short_year : 2000U + short_year;
        return user_tls_x509_date_to_unix(year, month, day, hour, minute, second, out_unix_time);
    }

    if (tlv->tag == 0x18U) {
        if (tlv->len != 15U || tlv->value[14] != 'Z') return -1;
        if (user_tls_x509_parse_decimal(tlv->value + 0U, 4U, &year) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 4U, 2U, &month) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 6U, 2U, &day) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 8U, 2U, &hour) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 10U, 2U, &minute) != 0 ||
            user_tls_x509_parse_decimal(tlv->value + 12U, 2U, &second) != 0) {
            return -1;
        }
        return user_tls_x509_date_to_unix(year, month, day, hour, minute, second, out_unix_time);
    }

    return -1;
}

static USER_CODE int user_tls_x509_parse_validity(user_tls_x509_cert_t* cert, const user_tls_x509_tlv_t* validity_tlv) {
    uint32_t off = 0;
    user_tls_x509_tlv_t not_before;
    user_tls_x509_tlv_t not_after;

    if (!cert || !validity_tlv || validity_tlv->tag != 0x30U) return -1;
    if (user_tls_x509_read_tlv(validity_tlv->value, validity_tlv->len, &off, &not_before) != 0 ||
        user_tls_x509_read_tlv(validity_tlv->value, validity_tlv->len, &off, &not_after) != 0 ||
        off != validity_tlv->len) {
        return -1;
    }
    if (user_tls_x509_parse_time_tlv(&not_before, &cert->not_before_unix) != 0 ||
        user_tls_x509_parse_time_tlv(&not_after, &cert->not_after_unix) != 0) {
        return -1;
    }
    return cert->not_before_unix <= cert->not_after_unix ? 0 : -1;
}

static USER_CODE int user_tls_x509_parse_uint32_integer(const user_tls_x509_tlv_t* integer_tlv, uint32_t* out_value) {
    uint32_t value = 0;
    uint32_t index = 0;

    if (!integer_tlv || !out_value || integer_tlv->tag != 0x02U || integer_tlv->len == 0U) return -1;
    if (integer_tlv->value[0] == 0x00U) {
        if (integer_tlv->len == 1U) return -1;
        index = 1U;
    } else if ((integer_tlv->value[0] & 0x80U) != 0U) {
        return -1;
    }
    if (integer_tlv->len - index > 4U) return -1;
    while (index < integer_tlv->len) {
        value = (value << 8) | integer_tlv->value[index++];
    }
    if (value == 0U) return -1;
    *out_value = value;
    return 0;
}

static USER_CODE int user_tls_x509_parse_dns_names(user_tls_x509_cert_t* cert, const uint8_t* der, uint32_t der_len) {
    uint32_t off = 0;
    user_tls_x509_tlv_t names_seq;

    if (!cert || !der) return -1;
    if (user_tls_x509_read_tlv(der, der_len, &off, &names_seq) != 0 || names_seq.tag != 0x30U || off != der_len) {
        return -1;
    }

    off = 0;
    while (off < names_seq.len) {
        user_tls_x509_tlv_t general_name;

        if (user_tls_x509_read_tlv(names_seq.value, names_seq.len, &off, &general_name) != 0) return -1;
        if (general_name.tag == 0x82U) {
            if (general_name.len == 0U) return -1;
            if (cert->dns_name_count >= USER_TLS_X509_MAX_DNS_NAMES) continue;
            cert->dns_names[cert->dns_name_count].data = general_name.value;
            cert->dns_names[cert->dns_name_count].len = general_name.len;
            cert->dns_name_count++;
        }
    }
    return 0;
}

static USER_CODE int user_tls_x509_parse_extensions(user_tls_x509_cert_t* cert, const user_tls_x509_tlv_t* ext_wrapper_tlv) {
    uint32_t off = 0;
    user_tls_x509_tlv_t ext_seq;
    int saw_san = 0;

    if (!cert || !ext_wrapper_tlv || ext_wrapper_tlv->tag != 0xa3U) return -1;
    if (user_tls_x509_read_tlv(ext_wrapper_tlv->value, ext_wrapper_tlv->len, &off, &ext_seq) != 0 ||
        ext_seq.tag != 0x30U || off != ext_wrapper_tlv->len) {
        return -1;
    }

    off = 0;
    while (off < ext_seq.len) {
        uint32_t ext_off = 0;
        user_tls_x509_tlv_t ext_tlv;
        user_tls_x509_tlv_t oid_tlv;
        user_tls_x509_tlv_t value_tlv;

        if (user_tls_x509_read_tlv(ext_seq.value, ext_seq.len, &off, &ext_tlv) != 0 || ext_tlv.tag != 0x30U) return -1;
        if (user_tls_x509_read_tlv(ext_tlv.value, ext_tlv.len, &ext_off, &oid_tlv) != 0 || oid_tlv.tag != 0x06U) return -1;

        if (ext_off < ext_tlv.len && ext_tlv.value[ext_off] == 0x01U) {
            user_tls_x509_tlv_t critical_tlv;

            if (user_tls_x509_read_tlv(ext_tlv.value, ext_tlv.len, &ext_off, &critical_tlv) != 0 ||
                critical_tlv.tag != 0x01U || critical_tlv.len != 1U) {
                return -1;
            }
        }

        if (user_tls_x509_read_tlv(ext_tlv.value, ext_tlv.len, &ext_off, &value_tlv) != 0 ||
            value_tlv.tag != 0x04U || ext_off != ext_tlv.len) {
            return -1;
        }

        if (user_tls_x509_mem_equals(oid_tlv.value, oid_tlv.len,
                                     user_tls_x509_oid_subject_alt_name,
                                     (uint32_t)sizeof(user_tls_x509_oid_subject_alt_name))) {
            if (saw_san) return -1;
            if (user_tls_x509_parse_dns_names(cert, value_tlv.value, value_tlv.len) != 0) return -1;
            saw_san = 1;
        }
    }

    return 0;
}

static USER_CODE int user_tls_x509_parse_spki(user_tls_x509_cert_t* cert, const user_tls_x509_tlv_t* spki_tlv) {
    uint32_t off = 0;
    uint32_t alg_off = 0;
    uint32_t key_off = 0;
    user_tls_x509_tlv_t alg_tlv;
    user_tls_x509_tlv_t key_tlv;
    user_tls_x509_tlv_t oid_tlv;
    user_tls_x509_tlv_t null_tlv;
    user_tls_x509_tlv_t rsa_seq;
    user_tls_x509_tlv_t modulus_tlv;
    user_tls_x509_tlv_t exponent_tlv;
    uint32_t modulus_index = 0;

    if (!cert || !spki_tlv || spki_tlv->tag != 0x30U) return -1;
    cert->subject_public_key_info.data = spki_tlv->raw;
    cert->subject_public_key_info.len = spki_tlv->raw_len;

    if (user_tls_x509_read_tlv(spki_tlv->value, spki_tlv->len, &off, &alg_tlv) != 0 ||
        user_tls_x509_read_tlv(spki_tlv->value, spki_tlv->len, &off, &key_tlv) != 0 ||
        off != spki_tlv->len || alg_tlv.tag != 0x30U || key_tlv.tag != 0x03U) {
        return -1;
    }

    if (user_tls_x509_read_tlv(alg_tlv.value, alg_tlv.len, &alg_off, &oid_tlv) != 0 || oid_tlv.tag != 0x06U) return -1;
    if (!user_tls_x509_mem_equals(oid_tlv.value, oid_tlv.len,
                                  user_tls_x509_oid_rsa_encryption,
                                  (uint32_t)sizeof(user_tls_x509_oid_rsa_encryption))) {
        return -1;
    }
    if (alg_off < alg_tlv.len) {
        if (user_tls_x509_read_tlv(alg_tlv.value, alg_tlv.len, &alg_off, &null_tlv) != 0 ||
            null_tlv.tag != 0x05U || null_tlv.len != 0U) {
            return -1;
        }
    }
    if (alg_off != alg_tlv.len) return -1;

    if (key_tlv.len < 1U || key_tlv.value[0] != 0x00U) return -1;
    if (user_tls_x509_read_tlv(key_tlv.value + 1U, key_tlv.len - 1U, &key_off, &rsa_seq) != 0 ||
        rsa_seq.tag != 0x30U || key_off != key_tlv.len - 1U) {
        return -1;
    }

    key_off = 0;
    if (user_tls_x509_read_tlv(rsa_seq.value, rsa_seq.len, &key_off, &modulus_tlv) != 0 ||
        user_tls_x509_read_tlv(rsa_seq.value, rsa_seq.len, &key_off, &exponent_tlv) != 0 ||
        key_off != rsa_seq.len || modulus_tlv.tag != 0x02U) {
        return -1;
    }

    if (modulus_tlv.len == 0U) return -1;
    if (modulus_tlv.value[0] == 0x00U) {
        if (modulus_tlv.len == 1U) return -1;
        modulus_index = 1U;
    } else if ((modulus_tlv.value[0] & 0x80U) != 0U) {
        return -1;
    }
    cert->rsa_modulus.data = modulus_tlv.value + modulus_index;
    cert->rsa_modulus.len = modulus_tlv.len - modulus_index;
    if (cert->rsa_modulus.len == 0U) return -1;

    return user_tls_x509_parse_uint32_integer(&exponent_tlv, &cert->rsa_exponent);
}

int USER_CODE user_tls_x509_parse_leaf(user_tls_x509_cert_t* out_cert, const uint8_t* der, uint32_t der_len) {
    uint32_t off = 0;
    uint32_t tbs_off = 0;
    int saw_extensions = 0;
    user_tls_x509_tlv_t cert_tlv;
    user_tls_x509_tlv_t tbs_tlv;
    user_tls_x509_tlv_t sig_alg_tlv;
    user_tls_x509_tlv_t sig_value_tlv;
    user_tls_x509_tlv_t field_tlv;

    if (!out_cert || !der || der_len == 0U) return -1;
    memset(out_cert, 0, sizeof(*out_cert));
    out_cert->der = der;
    out_cert->der_len = der_len;

    if (user_tls_x509_read_tlv(der, der_len, &off, &cert_tlv) != 0 ||
        cert_tlv.tag != 0x30U || off != der_len || cert_tlv.raw_len != der_len) {
        return -1;
    }
    off = 0;
    if (user_tls_x509_read_tlv(cert_tlv.value, cert_tlv.len, &off, &tbs_tlv) != 0 ||
        user_tls_x509_read_tlv(cert_tlv.value, cert_tlv.len, &off, &sig_alg_tlv) != 0 ||
        user_tls_x509_read_tlv(cert_tlv.value, cert_tlv.len, &off, &sig_value_tlv) != 0 ||
        off != cert_tlv.len || tbs_tlv.tag != 0x30U || sig_alg_tlv.tag != 0x30U ||
        sig_value_tlv.tag != 0x03U) {
        return -1;
    }

    out_cert->tbs_certificate.data = tbs_tlv.raw;
    out_cert->tbs_certificate.len = tbs_tlv.raw_len;

    if (tbs_tlv.len == 0U) return -1;
    field_tlv.tag = 0U;
    if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0) return -1;
    if (field_tlv.tag == 0xa0U) {
        uint32_t version_off = 0;
        user_tls_x509_tlv_t version_tlv;

        if (user_tls_x509_read_tlv(field_tlv.value, field_tlv.len, &version_off, &version_tlv) != 0 ||
            version_tlv.tag != 0x02U || version_off != field_tlv.len) {
            return -1;
        }
        if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0) return -1;
    }

    if (field_tlv.tag != 0x02U) return -1;
    if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0 || field_tlv.tag != 0x30U) return -1;
    if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0 || field_tlv.tag != 0x30U) return -1;
    if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0 ||
        user_tls_x509_parse_validity(out_cert, &field_tlv) != 0) {
        return -1;
    }
    if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0 || field_tlv.tag != 0x30U) return -1;
    if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0 ||
        user_tls_x509_parse_spki(out_cert, &field_tlv) != 0) {
        return -1;
    }

    while (tbs_off < tbs_tlv.len) {
        if (user_tls_x509_read_tlv(tbs_tlv.value, tbs_tlv.len, &tbs_off, &field_tlv) != 0) return -1;
        if (field_tlv.tag == 0xa3U) {
            if (saw_extensions || user_tls_x509_parse_extensions(out_cert, &field_tlv) != 0) return -1;
            saw_extensions = 1;
        } else if (field_tlv.tag != 0x81U && field_tlv.tag != 0x82U) {
            return -1;
        }
    }

    return 0;
}

int USER_CODE user_tls_x509_spki_sha256(const user_tls_x509_cert_t* cert,
                                        uint8_t out_hash[USER_TLS_SHA256_DIGEST_SIZE]) {
    if (!cert || !out_hash || !cert->subject_public_key_info.data || cert->subject_public_key_info.len == 0U) return -1;
    user_tls_sha256(cert->subject_public_key_info.data, cert->subject_public_key_info.len, out_hash);
    return 0;
}

int USER_CODE user_tls_x509_hostname_matches(const user_tls_x509_cert_t* cert, const char* hostname) {
    uint32_t host_len;

    if (!cert || !hostname || hostname[0] == '\0') return -1;
    host_len = (uint32_t)user_strlen(hostname);

    for (uint32_t i = 0; i < cert->dns_name_count; i++) {
        const user_tls_x509_span_t* name = &cert->dns_names[i];

        if (name->len >= 3U && name->data[0] == '*' && name->data[1] == '.') {
            uint32_t suffix_len = name->len - 1U;
            uint32_t prefix_len;
            int wildcard_ok = 1;

            if (host_len <= suffix_len) continue;
            for (uint32_t j = 2U; j < name->len; j++) {
                if (name->data[j] == '*') wildcard_ok = 0;
            }
            if (!wildcard_ok) continue;
            if (!user_tls_x509_casecmp_tail(hostname + (host_len - suffix_len), name->data + 1U, suffix_len)) continue;
            prefix_len = host_len - suffix_len;
            if (prefix_len == 0U) continue;
            wildcard_ok = 1;
            for (uint32_t j = 0; j < prefix_len; j++) {
                if (hostname[j] == '.') wildcard_ok = 0;
            }
            if (wildcard_ok) return 0;
            continue;
        }

        if (user_tls_x509_casecmp_span_cstr(name, hostname)) return 0;
    }

    return -1;
}

int USER_CODE user_tls_x509_is_valid_at(const user_tls_x509_cert_t* cert, uint64_t unix_time) {
    if (!cert) return -1;
    if (cert->not_before_unix > cert->not_after_unix) return -1;
    return (unix_time >= cert->not_before_unix && unix_time <= cert->not_after_unix) ? 0 : -1;
}

int USER_CODE user_tls_x509_selftest(char* detail, uint32_t detail_len) {
    user_tls_x509_cert_t cert;
    user_tls_x509_tlv_t time_tlv;
    uint8_t spki_hash[USER_TLS_SHA256_DIGEST_SIZE];
    uint64_t generalized_time = 0;
    char pin_detail[48];
    uint32_t off = 0;

    if (!detail || detail_len == 0U) return -1;
    detail[0] = '\0';
    pin_detail[0] = '\0';

    if (user_tls_x509_parse_leaf(&cert, user_tls_x509_test_cert_der, sizeof(user_tls_x509_test_cert_der)) != 0) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_parse_fail);
        return -1;
    }
    if (cert.not_before_unix != 1776544057ULL || cert.not_after_unix != 1847824057ULL) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_time_fail);
        return -1;
    }
    if (cert.rsa_exponent != 65537U || cert.rsa_modulus.len != 128U || cert.dns_name_count != 3U) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_rsa_fail);
        return -1;
    }

    if (user_tls_x509_read_tlv(user_tls_x509_generalized_time_tc, sizeof(user_tls_x509_generalized_time_tc),
                               &off, &time_tlv) != 0 ||
        off != sizeof(user_tls_x509_generalized_time_tc) ||
        user_tls_x509_parse_time_tlv(&time_tlv, &generalized_time) != 0 ||
        generalized_time != 2556241445ULL) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_time_tc_fail);
        return -1;
    }

    if (user_tls_x509_spki_sha256(&cert, spki_hash) != 0 ||
        !user_memeq_consttime(spki_hash, user_tls_x509_test_spki_hash, sizeof(spki_hash))) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_spki_fail);
        return -1;
    }

    if (user_tls_x509_hostname_matches(&cert, "test.example.com") != 0 ||
        user_tls_x509_hostname_matches(&cert, "API.TEST.EXAMPLE.COM") != 0 ||
        user_tls_x509_hostname_matches(&cert, "edge.svc.example.com") != 0 ||
        user_tls_x509_hostname_matches(&cert, "deep.edge.svc.example.com") == 0 ||
        user_tls_x509_hostname_matches(&cert, "missing.example.com") == 0) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_hostname_fail);
        return -1;
    }

    if (user_tls_x509_is_valid_at(&cert, cert.not_before_unix) != 0 ||
        user_tls_x509_is_valid_at(&cert, cert.not_after_unix) != 0 ||
        user_tls_x509_is_valid_at(&cert, cert.not_before_unix - 1ULL) == 0 ||
        user_tls_x509_is_valid_at(&cert, cert.not_after_unix + 1ULL) == 0) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_validity_fail);
        return -1;
    }

    if (user_tls_x509_parse_leaf(&cert, user_tls_x509_test_cert_der, sizeof(user_tls_x509_test_cert_der) - 1U) == 0) {
        user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_reject_fail);
        return -1;
    }

    if (user_tls_pins_match_host("test.example.com", spki_hash) != 0 ||
        user_tls_pins_match_host("missing.example.com", spki_hash) == 0 ||
        user_tls_pins_selftest(pin_detail, sizeof(pin_detail)) != 0) {
        user_tls_x509_copy_detail(detail, detail_len,
                                  pin_detail[0] != '\0' ? pin_detail : user_tls_x509_selftest_spki_fail);
        return -1;
    }

    user_tls_x509_copy_detail(detail, detail_len, user_tls_x509_selftest_ok);
    return 0;
}
