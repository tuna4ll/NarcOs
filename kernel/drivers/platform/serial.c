#include "serial.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);

#define COM1_PORT 0x3F8
#define SERIAL_TIMEOUT 0x10000
#define SERIAL_RING_BUFFER_SIZE 4096

static int serial_ready = 0;
static char serial_ring_buffer[SERIAL_RING_BUFFER_SIZE];
static uint32_t serial_ring_head = 0;
static uint32_t serial_ring_size = 0;

static void serial_ring_push(char c) {
    if (c == '\r') return;

    serial_ring_buffer[serial_ring_head] = c;
    serial_ring_head = (serial_ring_head + 1U) % SERIAL_RING_BUFFER_SIZE;
    if (serial_ring_size < SERIAL_RING_BUFFER_SIZE) serial_ring_size++;
}

static int serial_wait_tx_ready() {
    for (uint32_t i = 0; i < SERIAL_TIMEOUT; i++) {
        if ((inb(COM1_PORT + 5) & 0x20U) != 0U) return 1;
    }
    return 0;
}

void serial_init() {
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x80);
    outb(COM1_PORT + 0, 0x01);
    outb(COM1_PORT + 1, 0x00);
    outb(COM1_PORT + 3, 0x03);
    outb(COM1_PORT + 2, 0xC7);
    outb(COM1_PORT + 4, 0x03);
    serial_ready = 1;
}

int serial_is_ready() {
    return serial_ready;
}

void serial_write_char(char c) {
    serial_ring_push(c);
    if (!serial_ready) return;
    if (c == '\n') serial_write_char('\r');
    if (!serial_wait_tx_ready()) return;
    outb(COM1_PORT, (uint8_t)c);
}

void serial_write(const char* text) {
    if (!text) return;
    while (*text != '\0') serial_write_char(*text++);
}

void serial_write_line(const char* text) {
    serial_write(text);
    serial_write_char('\n');
}

void serial_write_hex32(uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9 - i] = hex[(value >> (i * 4)) & 0x0F];
    }
    buf[10] = '\0';
    serial_write(buf);
}

void serial_write_hex64(uint64_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char buf[19];

    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        buf[17 - i] = hex[(uint8_t)((value >> (i * 4)) & 0x0FU)];
    }
    buf[18] = '\0';
    serial_write(buf);
}

int serial_copy_ring_buffer(char* out, int max_len) {
    uint32_t start;
    uint32_t count;

    if (!out || max_len <= 0) return 0;
    if (max_len == 1) {
        out[0] = '\0';
        return 0;
    }

    count = serial_ring_size;
    if ((uint32_t)(max_len - 1) < count) count = (uint32_t)(max_len - 1);
    start = (serial_ring_head + SERIAL_RING_BUFFER_SIZE - serial_ring_size) % SERIAL_RING_BUFFER_SIZE;
    if (count < serial_ring_size) {
        start = (serial_ring_head + SERIAL_RING_BUFFER_SIZE - count) % SERIAL_RING_BUFFER_SIZE;
    }

    for (uint32_t i = 0; i < count; i++) {
        out[i] = serial_ring_buffer[(start + i) % SERIAL_RING_BUFFER_SIZE];
    }
    out[count] = '\0';
    return (int)count;
}
