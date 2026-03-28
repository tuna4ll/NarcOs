#include "ata.h"
static inline void outb_port(uint16_t port, uint8_t val) {
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );
}
static inline uint8_t inb_port(uint16_t port) {
    uint8_t ret;
    asm volatile ( "inb %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}
static inline void outw_port(uint16_t port, uint16_t val) {
    asm volatile ( "outw %0, %1" : : "a"(val), "Nd"(port) );
}
static inline uint16_t inw_port(uint16_t port) {
    uint16_t ret;
    asm volatile ( "inw %1, %0" : "=a"(ret) : "Nd"(port) );
    return ret;
}
static void ata_wait_bsy() {
    while(inb_port(0x1F7) & 0x80);
}
static void ata_wait_drq() {
    while(!(inb_port(0x1F7) & 0x08));
}
void ata_read_sector(uint32_t lba, uint8_t *buffer) {
    ata_wait_bsy();
    outb_port(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb_port(0x1F2, 1);
    outb_port(0x1F3, (uint8_t)lba);
    outb_port(0x1F4, (uint8_t)(lba >> 8));
    outb_port(0x1F5, (uint8_t)(lba >> 16));
    outb_port(0x1F7, 0x20); 
    ata_wait_bsy();
    ata_wait_drq();
    for (int i = 0; i < 256; i++) {
        uint16_t w = inw_port(0x1F0);
        buffer[i * 2] = (uint8_t)(w & 0xFF);
        buffer[i * 2 + 1] = (uint8_t)(w >> 8);
    }
}
void ata_write_sector(uint32_t lba, uint8_t *buffer) {
    ata_wait_bsy();
    outb_port(0x1F6, 0xE0 | ((lba >> 24) & 0x0F));
    outb_port(0x1F2, 1);
    outb_port(0x1F3, (uint8_t)lba);
    outb_port(0x1F4, (uint8_t)(lba >> 8));
    outb_port(0x1F5, (uint8_t)(lba >> 16));
    outb_port(0x1F7, 0x30);
    ata_wait_bsy();
    ata_wait_drq();
    for (int i = 0; i < 256; i++) {
        uint16_t w = buffer[i * 2] | (buffer[i * 2 + 1] << 8);
        outw_port(0x1F0, w);
    }
    outb_port(0x1F7, 0xE7);
    ata_wait_bsy();
}
