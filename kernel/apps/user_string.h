#ifndef USER_STRING_H
#define USER_STRING_H

#include <stddef.h>
#include <stdint.h>

#ifndef USER_CODE
#define USER_CODE __attribute__((section(".user_code")))
#endif

static USER_CODE int user_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static USER_CODE int user_strncmp(const char* s1, const char* s2, size_t n) {
    while (n != 0U && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0U) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static USER_CODE size_t user_strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') len++;
    return len;
}

static USER_CODE char* user_strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static USER_CODE void* user_memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    while (n-- != 0U) *p++ = (unsigned char)c;
    return s;
}

static USER_CODE void* user_memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    while (n-- != 0U) *d++ = *s++;
    return dest;
}

static USER_CODE int user_memcmp(const void* s1, const void* s2, size_t n) {
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;

    while (n != 0U) {
        if (*a != *b) return (int)*a - (int)*b;
        a++;
        b++;
        n--;
    }
    return 0;
}

static USER_CODE int user_memeq_consttime(const void* s1, const void* s2, size_t n) {
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    unsigned char diff = 0;

    while (n-- != 0U) diff |= (unsigned char)(*a++ ^ *b++);
    return diff == 0U;
}

#endif
