#include "string.h"
#include <stdint.h>

typedef uintptr_t __attribute__((__may_alias__)) word_t;

static word_t string_repeat_byte(unsigned char value) {
    word_t word = (word_t)value;
    word |= word << 8;
    word |= word << 16;
#if UINTPTR_MAX > 0xFFFFFFFFU
    word |= word << 32;
#endif
    return word;
}

static int string_word_has_zero(word_t value) {
    word_t low_bits = string_repeat_byte(0x01);
    word_t high_bits = string_repeat_byte(0x80);
    return ((value - low_bits) & ~value & high_bits) != 0;
}

int strcmp(const char* s1, const char* s2) {
    while(*s1 && (*s1 == *s2)) {
        s1++; s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
int strncmp(const char* s1, const char* s2, size_t n) {
    while(n && *s1 && (*s1 == *s2)) {
        s1++; s2++; n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}
size_t strlen(const char* str) {
    const char* s = str;

    while (((uintptr_t)s & (sizeof(word_t) - 1U)) != 0U) {
        if (*s == '\0') return (size_t)(s - str);
        s++;
    }

    while (!string_word_has_zero(*(const word_t*)s)) {
        s += sizeof(word_t);
    }

    while (*s != '\0') s++;
    return (size_t)(s - str);
}
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while((*d++ = *src++));
    return dest;
}
char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';
    return dest;
}

int endsWith(const char* str, const char* suffix) {
    if (!str || !suffix) return 0;
    size_t len_str = strlen(str);
    size_t len_suffix = strlen(suffix);
    if (len_suffix > len_str) return 0;
    return strcmp(str + len_str - len_suffix, suffix) == 0;
}

void* memset(void* s, int c, size_t n) {
    unsigned char* p = (unsigned char*)s;
    unsigned char value = (unsigned char)c;

    while (n != 0U && ((uintptr_t)p & (sizeof(word_t) - 1U)) != 0U) {
        *p++ = value;
        n--;
    }

    if (n >= sizeof(word_t)) {
        word_t fill = string_repeat_byte(value);
        word_t* word_ptr = (word_t*)p;

        while (n >= sizeof(word_t)) {
            *word_ptr++ = fill;
            n -= sizeof(word_t);
        }
        p = (unsigned char*)word_ptr;
    }

    while (n != 0U) {
        *p++ = value;
        n--;
    }
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0U) return dest;

    if ((((uintptr_t)d ^ (uintptr_t)s) & (sizeof(word_t) - 1U)) == 0U) {
        while (n != 0U && ((uintptr_t)d & (sizeof(word_t) - 1U)) != 0U) {
            *d++ = *s++;
            n--;
        }

        if (n >= sizeof(word_t)) {
            word_t* dw = (word_t*)d;
            const word_t* sw = (const word_t*)s;

            while (n >= sizeof(word_t)) {
                *dw++ = *sw++;
                n -= sizeof(word_t);
            }

            d = (unsigned char*)dw;
            s = (const unsigned char*)sw;
        }
    }

    while (n != 0U) {
        *d++ = *s++;
        n--;
    }
    return dest;
}
