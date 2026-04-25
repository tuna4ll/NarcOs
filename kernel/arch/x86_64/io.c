#include "io.h"

uint8_t inb(uint16_t port) {
    uint8_t value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint16_t inw(uint16_t port) {
    uint16_t value;

    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t inl(uint16_t port) {
    uint32_t value;

    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}
