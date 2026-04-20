#ifndef USER_LIB_H
#define USER_LIB_H

#include <stddef.h>
#include <stdint.h>
#include "user_abi.h"

#define USER_STDIN 0
#define USER_STDOUT 1
#define USER_STDERR 2

static inline size_t userlib_strlen(const char* text) {
    size_t len = 0;

    if (!text) return 0;
    while (text[len] != '\0') len++;
    return len;
}

static inline int userlib_strcmp(const char* left, const char* right) {
    while (*left && (*left == *right)) {
        left++;
        right++;
    }
    return *(const unsigned char*)left - *(const unsigned char*)right;
}

static inline void* userlib_memcpy(void* dest, const void* src, size_t len) {
    unsigned char* out = (unsigned char*)dest;
    const unsigned char* in = (const unsigned char*)src;

    while (len-- != 0U) *out++ = *in++;
    return dest;
}

static inline int userlib_write_all(int fd, const void* data, uint32_t len) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t written = 0;

    while (written < len) {
        int rc = user_write(fd, bytes + written, len - written);
        if (rc <= 0) return -1;
        written += (uint32_t)rc;
    }
    return 0;
}

static inline int userlib_print_fd(int fd, const char* text) {
    return userlib_write_all(fd, text, (uint32_t)userlib_strlen(text));
}

static inline int userlib_print(const char* text) {
    return userlib_print_fd(USER_STDOUT, text);
}

static inline int userlib_println(const char* text) {
    if (userlib_print_fd(USER_STDOUT, text ? text : "") != 0) return -1;
    return userlib_write_all(USER_STDOUT, "\n", 1U);
}

static inline int userlib_print_error(const char* text) {
    if (userlib_print_fd(USER_STDERR, text ? text : "") != 0) return -1;
    return userlib_write_all(USER_STDERR, "\n", 1U);
}

static inline int userlib_print_u32_fd(int fd, uint32_t value) {
    char digits[16];
    uint32_t count = 0;

    if (value == 0U) return userlib_write_all(fd, "0", 1U);
    while (value != 0U && count < (uint32_t)sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    }
    while (count > 0U) {
        count--;
        if (userlib_write_all(fd, &digits[count], 1U) != 0) return -1;
    }
    return 0;
}

static inline int userlib_print_i32_fd(int fd, int32_t value) {
    uint32_t magnitude;

    if (value < 0) {
        if (userlib_write_all(fd, "-", 1U) != 0) return -1;
        magnitude = (uint32_t)(-(value + 1)) + 1U;
        return userlib_print_u32_fd(fd, magnitude);
    }
    return userlib_print_u32_fd(fd, (uint32_t)value);
}

static inline int userlib_parse_i32(const char* text, int* out_value) {
    int sign = 1;
    int value = 0;

    if (!text || !out_value || *text == '\0') return -1;
    if (*text == '-') {
        sign = -1;
        text++;
    }
    if (*text < '0' || *text > '9') return -1;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    if (*text != '\0') return -1;
    *out_value = value * sign;
    return 0;
}

static inline int userlib_min_int(int left, int right) {
    return left < right ? left : right;
}

#endif
