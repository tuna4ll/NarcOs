#include "user_lib.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define EISDIR 21
#define ENOENT 2
#define ENOMEM 12
#define DOOM_ARENA_DEFAULT (9U * 1024U * 1024U)
#define DOOM_ARENA_MIN     (6U * 1024U * 1024U)
#define DOOM_FILE_MAGIC 0x44464C45U

typedef struct doom_file FILE;

typedef struct doom_block {
    uint32_t size;
    uint32_t free;
    struct doom_block* next;
} doom_block_t;

struct doom_file {
    uint32_t magic;
    int kind;
    int write_mode;
    uint32_t pos;
    uint32_t size;
    uint32_t capacity;
    char path[256];
    uint8_t* data;
};

int errno;
static uint8_t* doom_arena;
static uint32_t doom_arena_size;
static doom_block_t* doom_heap_head;
static FILE doom_stdin_file = {DOOM_FILE_MAGIC, 0, 0, 0, 0, 0, {0}, 0};
static FILE doom_stdout_file = {DOOM_FILE_MAGIC, 1, 0, 0, 0, 0, {0}, 0};
static FILE doom_stderr_file = {DOOM_FILE_MAGIC, 2, 0, 0, 0, 0, {0}, 0};
FILE* stdin = &doom_stdin_file;
FILE* stdout = &doom_stdout_file;
FILE* stderr = &doom_stderr_file;

int* __errno_location(void) {
    return &errno;
}

static uint64_t doom_udivmod64(uint64_t num, uint64_t den, uint64_t* rem) {
    uint64_t q = 0;
    uint64_t r = 0;

    if (den == 0U) {
        if (rem) *rem = 0U;
        return 0U;
    }
    for (int bit = 63; bit >= 0; bit--) {
        r = (r << 1) | ((num >> bit) & 1U);
        if (r >= den) {
            r -= den;
            q |= 1ULL << bit;
        }
    }
    if (rem) *rem = r;
    return q;
}

uint64_t __udivdi3(uint64_t num, uint64_t den) {
    return doom_udivmod64(num, den, 0);
}

uint64_t __umoddi3(uint64_t num, uint64_t den) {
    uint64_t rem;

    (void)doom_udivmod64(num, den, &rem);
    return rem;
}

int64_t __divdi3(int64_t num, int64_t den) {
    int neg = 0;
    uint64_t unum;
    uint64_t uden;
    uint64_t q;

    if (num < 0) {
        neg = !neg;
        unum = (uint64_t)(-(num + 1)) + 1U;
    } else {
        unum = (uint64_t)num;
    }
    if (den < 0) {
        neg = !neg;
        uden = (uint64_t)(-(den + 1)) + 1U;
    } else {
        uden = (uint64_t)den;
    }
    q = doom_udivmod64(unum, uden, 0);
    return neg ? -(int64_t)q : (int64_t)q;
}

int64_t __moddi3(int64_t num, int64_t den) {
    int neg = 0;
    uint64_t unum;
    uint64_t uden;
    uint64_t rem;

    if (num < 0) {
        neg = 1;
        unum = (uint64_t)(-(num + 1)) + 1U;
    } else {
        unum = (uint64_t)num;
    }
    if (den < 0) {
        uden = (uint64_t)(-(den + 1)) + 1U;
    } else {
        uden = (uint64_t)den;
    }
    (void)doom_udivmod64(unum, uden, &rem);
    return neg ? -(int64_t)rem : (int64_t)rem;
}

static uint32_t doom_align(uint32_t value) {
    return (value + 15U) & ~15U;
}

static int doom_heap_init(void) {
    uint32_t request = DOOM_ARENA_DEFAULT;

    if (doom_heap_head) return 0;
    while (request >= DOOM_ARENA_MIN) {
        doom_arena = (uint8_t*)user_malloc(request);
        if (doom_arena) break;
        request -= 1024U * 1024U;
    }
    if (!doom_arena) {
        errno = ENOMEM;
        return -1;
    }
    doom_arena_size = request;
    doom_heap_head = (doom_block_t*)doom_arena;
    doom_heap_head->size = request - sizeof(doom_block_t);
    doom_heap_head->free = 1U;
    doom_heap_head->next = 0;
    return 0;
}

void* malloc(size_t size) {
    doom_block_t* block;
    uint32_t need;

    if (size == 0U) return 0;
    if (doom_heap_init() != 0) return 0;
    need = doom_align((uint32_t)size);
    for (block = doom_heap_head; block; block = block->next) {
        if (!block->free || block->size < need) continue;
        if (block->size > need + sizeof(doom_block_t) + 32U) {
            doom_block_t* split = (doom_block_t*)((uint8_t*)block + sizeof(doom_block_t) + need);

            split->size = block->size - need - sizeof(doom_block_t);
            split->free = 1U;
            split->next = block->next;
            block->size = need;
            block->next = split;
        }
        block->free = 0U;
        return (uint8_t*)block + sizeof(doom_block_t);
    }
    errno = ENOMEM;
    return 0;
}

void free(void* ptr) {
    doom_block_t* block;

    if (!ptr) return;
    block = (doom_block_t*)((uint8_t*)ptr - sizeof(doom_block_t));
    block->free = 1U;
    for (block = doom_heap_head; block; block = block->next) {
        while (block->next && block->free && block->next->free) {
            block->size += sizeof(doom_block_t) + block->next->size;
            block->next = block->next->next;
        }
    }
}

void* calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void* ptr = malloc(total);

    if (ptr) userlib_memset(ptr, 0, total);
    return ptr;
}

void* realloc(void* ptr, size_t size) {
    doom_block_t* block;
    void* out;

    if (!ptr) return malloc(size);
    if (size == 0U) {
        free(ptr);
        return 0;
    }
    block = (doom_block_t*)((uint8_t*)ptr - sizeof(doom_block_t));
    if (block->size >= size) return ptr;
    out = malloc(size);
    if (!out) return 0;
    userlib_memcpy(out, ptr, block->size);
    free(ptr);
    return out;
}

void* memset(void* dest, int value, size_t len) { return userlib_memset(dest, value, len); }
void* memcpy(void* dest, const void* src, size_t len) { return userlib_memcpy(dest, src, len); }

void* memmove(void* dest, const void* src, size_t len) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;

    if (d == s || len == 0U) return dest;
    if (d < s) {
        for (size_t i = 0; i < len; i++) d[i] = s[i];
    } else {
        while (len-- != 0U) d[len] = s[len];
    }
    return dest;
}

int memcmp(const void* left, const void* right, size_t len) {
    const uint8_t* a = (const uint8_t*)left;
    const uint8_t* b = (const uint8_t*)right;

    for (size_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return (int)a[i] - (int)b[i];
    }
    return 0;
}

size_t strlen(const char* s) { return userlib_strlen(s); }
int strcmp(const char* a, const char* b) { return userlib_strcmp(a, b); }
int strncmp(const char* a, const char* b, size_t n) { return userlib_strncmp(a, b, n); }

char* strcpy(char* dest, const char* src) {
    char* out = dest;
    while ((*dest++ = *src++) != '\0') {}
    return out;
}

char* strncpy(char* dest, const char* src, size_t n) { return userlib_strncpy(dest, src, n); }

char* strcat(char* dest, const char* src) {
    strcpy(dest + strlen(dest), src);
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* out = dest;

    dest += strlen(dest);
    while (n-- != 0U && *src) *dest++ = *src++;
    *dest = '\0';
    return out;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return c == 0 ? (char*)s : 0;
}

char* strrchr(const char* s, int c) {
    const char* last = 0;

    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}

char* strstr(const char* haystack, const char* needle) {
    size_t needle_len = strlen(needle);

    if (needle_len == 0U) return (char*)haystack;
    for (; *haystack; haystack++) {
        if (strncmp(haystack, needle, needle_len) == 0) return (char*)haystack;
    }
    return 0;
}

char* strdup(const char* s) {
    size_t len = strlen(s) + 1U;
    char* out = (char*)malloc(len);

    if (out) memcpy(out, s, len);
    return out;
}

int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - ('a' - 'A') : c; }
int isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isprint(int c) { return c >= 32 && c < 127; }
int abs(int v) { return v < 0 ? -v : v; }

int strcasecmp(const char* a, const char* b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int strncasecmp(const char* a, const char* b, size_t n) {
    while (n != 0U && *a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0U) return 0;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

int atoi(const char* s) {
    int sign = 1;
    int value = 0;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (isdigit((unsigned char)*s)) {
        value = value * 10 + (*s - '0');
        s++;
    }
    return value * sign;
}

#ifndef NARCOS_DOOM_NO_FLOAT
double atof(const char* s) {
    int sign = 1;
    double value = 0.0;
    double place = 0.1;

    while (isspace((unsigned char)*s)) s++;
    if (*s == '-') {
        sign = -1;
        s++;
    }
    while (isdigit((unsigned char)*s)) value = value * 10.0 + (double)(*s++ - '0');
    if (*s == '.') {
        s++;
        while (isdigit((unsigned char)*s)) {
            value += (double)(*s++ - '0') * place;
            place *= 0.1;
        }
    }
    return sign < 0 ? -value : value;
}
#endif

static void fmt_putc(char** out, size_t* remaining, int c) {
    if (*remaining > 1U) {
        **out = (char)c;
        (*out)++;
        (*remaining)--;
    }
}

static void fmt_puts(char** out, size_t* remaining, const char* s) {
    if (!s) s = "(null)";
    while (*s) fmt_putc(out, remaining, *s++);
}

static void fmt_uint(char** out, size_t* remaining, uint32_t value, uint32_t base, int upper) {
    char tmp[32];
    int pos = 0;

    if (value == 0U) {
        fmt_putc(out, remaining, '0');
        return;
    }
    while (value != 0U && pos < (int)sizeof(tmp)) {
        uint32_t digit = value % base;
        tmp[pos++] = digit < 10U ? (char)('0' + digit) : (char)((upper ? 'A' : 'a') + digit - 10U);
        value /= base;
    }
    while (pos > 0) fmt_putc(out, remaining, tmp[--pos]);
}

static void fmt_uint_padded(char** out, size_t* remaining, uint32_t value, uint32_t base,
                            int upper, int width, int precision, int zero_pad) {
    char tmp[32];
    int pos = 0;
    int digits;
    int zeroes;
    int spaces;

    if (value == 0U) {
        if (precision != 0) tmp[pos++] = '0';
    } else {
        while (value != 0U && pos < (int)sizeof(tmp)) {
            uint32_t digit = value % base;
            tmp[pos++] = digit < 10U ? (char)('0' + digit) : (char)((upper ? 'A' : 'a') + digit - 10U);
            value /= base;
        }
    }

    digits = pos;
    zeroes = precision > digits ? precision - digits : 0;
    if (precision < 0 && zero_pad && width > digits) {
        zeroes = width - digits;
    }
    spaces = width - digits - zeroes;
    while (spaces-- > 0) fmt_putc(out, remaining, ' ');
    while (zeroes-- > 0) fmt_putc(out, remaining, '0');
    while (pos > 0) fmt_putc(out, remaining, tmp[--pos]);
}

static void fmt_int_padded(char** out, size_t* remaining, int value, int width, int precision, int zero_pad) {
    uint32_t magnitude;

    if (value < 0) {
        fmt_putc(out, remaining, '-');
        magnitude = (uint32_t)(-(value + 1)) + 1U;
        if (width > 0) width--;
    } else {
        magnitude = (uint32_t)value;
    }
    fmt_uint_padded(out, remaining, magnitude, 10U, 0, width, precision, zero_pad);
}

static int doom_vsnprintf(char* buf, size_t len, const char* fmt, va_list ap) {
    char* out = buf;
    size_t remaining = len;
    char* start = buf;

    if (!buf || len == 0U) return 0;
    while (*fmt) {
        int long_mod = 0;
        int width = 0;
        int precision = -1;
        int zero_pad = 0;

        if (*fmt != '%') {
            fmt_putc(&out, &remaining, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '0') {
            zero_pad = 1;
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = precision * 10 + (*fmt - '0');
                fmt++;
            }
        }
        if (*fmt == 'l') {
            long_mod = 1;
            fmt++;
            if (*fmt == 'l') fmt++;
        }
        switch (*fmt) {
            case '%':
                fmt_putc(&out, &remaining, '%');
                break;
            case 'c':
                fmt_putc(&out, &remaining, va_arg(ap, int));
                break;
            case 's':
                fmt_puts(&out, &remaining, va_arg(ap, const char*));
                break;
            case 'd':
            case 'i':
                fmt_int_padded(&out, &remaining, va_arg(ap, int), width, precision, zero_pad);
                break;
            case 'u':
                fmt_uint_padded(&out, &remaining, va_arg(ap, uint32_t), 10U, 0, width, precision, zero_pad);
                break;
            case 'x':
                fmt_uint_padded(&out, &remaining, va_arg(ap, uint32_t), 16U, 0, width, precision, zero_pad);
                break;
            case 'X':
                fmt_uint_padded(&out, &remaining, va_arg(ap, uint32_t), 16U, 1, width, precision, zero_pad);
                break;
            case 'p':
                fmt_puts(&out, &remaining, "0x");
                fmt_uint(&out, &remaining, (uint32_t)(uintptr_t)va_arg(ap, void*), 16U, 0);
                break;
            case 'f': {
#ifndef NARCOS_DOOM_NO_FLOAT
                double d = va_arg(ap, double);
                int whole;
                int frac;

                if (d < 0.0) {
                    fmt_putc(&out, &remaining, '-');
                    d = -d;
                }
                whole = (int)d;
                frac = (int)((d - (double)whole) * 1000.0);
                fmt_int_padded(&out, &remaining, whole, 0, -1, 0);
                fmt_putc(&out, &remaining, '.');
                if (frac < 100) fmt_putc(&out, &remaining, '0');
                if (frac < 10) fmt_putc(&out, &remaining, '0');
                fmt_uint(&out, &remaining, (uint32_t)frac, 10U, 0);
#else
                fmt_puts(&out, &remaining, "0");
#endif
                break;
            }
            default:
                fmt_putc(&out, &remaining, '%');
                if (long_mod) fmt_putc(&out, &remaining, 'l');
                if (*fmt) fmt_putc(&out, &remaining, *fmt);
                break;
        }
        if (*fmt) fmt++;
    }
    *out = '\0';
    return (int)(out - start);
}

int vsnprintf(char* buf, size_t len, const char* fmt, va_list ap) {
    return doom_vsnprintf(buf, len, fmt, ap);
}

int snprintf(char* buf, size_t len, const char* fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = doom_vsnprintf(buf, len, fmt, ap);
    va_end(ap);
    return rc;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = doom_vsnprintf(buf, 4096U, fmt, ap);
    va_end(ap);
    return rc;
}

static int write_formatted(FILE* f, const char* text, int len) {
    (void)f;
    (void)text;
    return len;
}

int vfprintf(FILE* f, const char* fmt, va_list ap) {
    char buf[1024];
    int len = doom_vsnprintf(buf, sizeof(buf), fmt, ap);

    write_formatted(f, buf, len);
    return len;
}

int fprintf(FILE* f, const char* fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = vfprintf(f, fmt, ap);
    va_end(ap);
    return rc;
}

int printf(const char* fmt, ...) {
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return rc;
}

int putchar(int c) {
    return c;
}

int puts(const char* s) {
    (void)s;
    return 0;
}

static int parse_int_token(const char* s, int base, unsigned int* out) {
    unsigned int value = 0;
    int any = 0;

    while (isspace((unsigned char)*s)) s++;
    if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        int digit;

        if (*s >= '0' && *s <= '9') digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f') digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F') digit = *s - 'A' + 10;
        else break;
        if (digit >= base) break;
        value = value * (unsigned int)base + (unsigned int)digit;
        any = 1;
        s++;
    }
    if (!any) return 0;
    *out = value;
    return 1;
}

int sscanf(const char* str, const char* fmt, ...) {
    va_list ap;
    unsigned int* out;
    unsigned int value = 0;
    int base = 10;
    int ok;

    va_start(ap, fmt);
    out = va_arg(ap, unsigned int*);
    if (strstr(fmt, "%x") || strstr(fmt, "%X")) base = 16;
    else if (strstr(fmt, "%o")) base = 8;
    ok = parse_int_token(str, base, &value);
    if (ok) *out = value;
    va_end(ap);
    return ok ? 1 : 0;
}

FILE* fopen(const char* path, const char* mode) {
    FILE* f;
    int idx;

    if (!path || !mode) return 0;
    f = (FILE*)malloc(sizeof(FILE));
    if (!f) return 0;
    memset(f, 0, sizeof(FILE));
    f->magic = DOOM_FILE_MAGIC;
    f->kind = 3;
    f->write_mode = (strchr(mode, 'w') != 0);
    strncpy(f->path, path, sizeof(f->path) - 1U);
    if (f->write_mode) {
        (void)user_fs_touch(path);
        return f;
    }
    idx = user_fs_find_node(path);
    if (idx < 0) {
        errno = ENOENT;
        free(f);
        return 0;
    }
    {
        disk_fs_node_t node;

        if (user_fs_get_node_info(idx, &node) != 0 || node.flags != FS_NODE_FILE) {
            errno = EISDIR;
            free(f);
            return 0;
        }
        f->size = node.size;
    }
    return f;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* f) {
    uint32_t want;
    int rc;

    if (!ptr || !f || f->magic != DOOM_FILE_MAGIC || size == 0U || nmemb == 0U) return 0;
    want = (uint32_t)(size * nmemb);
    if (f->pos >= f->size) return 0;
    if (want > f->size - f->pos) want = f->size - f->pos;
    rc = user_fs_read_raw(f->path, ptr, want, f->pos);
    if (rc <= 0) return 0;
    f->pos += (uint32_t)rc;
    return (size_t)rc / size;
}

static int file_reserve(FILE* f, uint32_t need) {
    uint8_t* next;
    uint32_t cap;

    if (need <= f->capacity) return 0;
    cap = f->capacity == 0U ? 512U : f->capacity;
    while (cap < need) cap *= 2U;
    next = (uint8_t*)realloc(f->data, cap);
    if (!next) return -1;
    f->data = next;
    f->capacity = cap;
    return 0;
}

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* f) {
    uint32_t bytes = (uint32_t)(size * nmemb);

    if (!ptr || !f || size == 0U || nmemb == 0U) return 0;
    if (f == stdout) return userlib_write_all(USER_STDOUT, ptr, bytes) == 0 ? nmemb : 0;
    if (f == stderr) return userlib_write_all(USER_STDERR, ptr, bytes) == 0 ? nmemb : 0;
    if (f->magic != DOOM_FILE_MAGIC || !f->write_mode) return 0;
    if (file_reserve(f, f->pos + bytes) != 0) return 0;
    memcpy(f->data + f->pos, ptr, bytes);
    f->pos += bytes;
    if (f->pos > f->size) f->size = f->pos;
    return nmemb;
}

int fseek(FILE* f, long offset, int whence) {
    int32_t base = 0;
    int32_t next;

    if (!f || f->magic != DOOM_FILE_MAGIC) return -1;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = (int32_t)f->pos;
    else if (whence == SEEK_END) base = (int32_t)f->size;
    else return -1;
    next = base + (int32_t)offset;
    if (next < 0) return -1;
    f->pos = (uint32_t)next;
    return 0;
}

long ftell(FILE* f) {
    if (!f || f->magic != DOOM_FILE_MAGIC) return -1;
    return (long)f->pos;
}

int fflush(FILE* f) {
    if (!f || f == stdout || f == stderr || !f->write_mode) return 0;
    return user_fs_write_raw(f->path, f->data ? (const void*)f->data : (const void*)"", f->size) < 0 ? EOF : 0;
}

int fclose(FILE* f) {
    int rc;

    if (!f || f == stdin || f == stdout || f == stderr) return 0;
    rc = fflush(f);
    if (f->data) free(f->data);
    free(f);
    return rc;
}

int remove(const char* path) {
    return user_fs_delete(path);
}

int rename(const char* oldpath, const char* newpath) {
    int idx;
    disk_fs_node_t node;
    uint8_t* data;
    int rc;

    idx = user_fs_find_node(oldpath);
    if (idx < 0 || user_fs_get_node_info(idx, &node) != 0 || node.flags != FS_NODE_FILE) return -1;
    data = (uint8_t*)malloc(node.size);
    if (!data && node.size != 0U) return -1;
    rc = user_fs_read_raw(oldpath, data, node.size, 0U);
    if (rc != (int)node.size) {
        if (data) free(data);
        return -1;
    }
    (void)user_fs_delete(newpath);
    rc = user_fs_write_raw(newpath, data ? (const void*)data : (const void*)"", node.size);
    if (data) free(data);
    if (rc < 0) return -1;
    (void)user_fs_delete(oldpath);
    return 0;
}

int mkdir(const char* path, ...) {
    return user_fs_mkdir(path);
}

char* getenv(const char* name) {
    (void)name;
    return 0;
}

int system(const char* command) {
    (void)command;
    return -1;
}

int isatty(int fd) {
    (void)fd;
    return 0;
}

int fileno(void* stream) {
    if (stream == stdin) return USER_STDIN;
    if (stream == stderr) return USER_STDERR;
    return USER_STDOUT;
}

void exit(int code) {
    user_exit(code);
}

void abort(void) {
    user_exit(1);
}

#ifndef NARCOS_DOOM_NO_FLOAT
double fabs(double x) { return x < 0.0 ? -x : x; }

static double norm_angle(double x) {
    const double pi = 3.14159265358979323846;
    const double two_pi = 6.28318530717958647692;

    while (x > pi) x -= two_pi;
    while (x < -pi) x += two_pi;
    return x;
}

double sin(double x) {
    double x2;

    x = norm_angle(x);
    x2 = x * x;
    return x * (1.0 - x2 / 6.0 + (x2 * x2) / 120.0 - (x2 * x2 * x2) / 5040.0);
}

double cos(double x) {
    const double half_pi = 1.57079632679489661923;

    return sin(x + half_pi);
}

double tan(double x) {
    double c = cos(x);

    if (c > -0.000001 && c < 0.000001) return x < 0.0 ? -1000000.0 : 1000000.0;
    return sin(x) / c;
}

double atan(double x) {
    const double pi_over_2 = 1.57079632679489661923;
    int neg = x < 0.0;
    double result;

    if (neg) x = -x;
    if (x > 1.0) result = pi_over_2 - atan(1.0 / x);
    else {
        double x2 = x * x;
        result = x * (1.0 - x2 / 3.0 + (x2 * x2) / 5.0 - (x2 * x2 * x2) / 7.0);
    }
    return neg ? -result : result;
}
#endif
