#include "mouse.h"
#include "vbe.h"
#include <stdint.h>
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[4];
static int     mouse_x = 512;
static int     mouse_y = 384;
static int     left_button = 0;
void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 1) == 1) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 2) == 0) return;
        }
    }
}
void mouse_write(uint8_t write) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, write);
}
uint8_t mouse_read() {
    mouse_wait(0);
    return inb(0x60);
}
void init_mouse() {
    mouse_wait(1);
    outb(0x64, 0xA8); 
    mouse_write(0xFF);
    mouse_read();     
    mouse_read();     
    mouse_read();     
    mouse_write(0xF6);
    mouse_read();
    mouse_write(0xF4);
    mouse_read();
    mouse_wait(1);
    outb(0x64, 0x20);
    mouse_wait(0);
    uint8_t status = (inb(0x60) | 0x02) & ~0x20;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    mouse_wait(1);
    outb(0x64, 0xAE);
}
void handle_mouse() {
    uint8_t status = inb(0x64);
    if (!(status & 0x01) || !(status & 0x20)) {
        outb(0x20, 0x20);
        outb(0xA0, 0x20);
        return;
    }
    uint8_t data = inb(0x60);
    if (mouse_cycle == 0 && !(data & 0x08)) {
        outb(0x20, 0x20);
        outb(0xA0, 0x20);
        return; 
    }
    mouse_packet[mouse_cycle++] = data;
    if (mouse_cycle == 3) {
        mouse_cycle = 0;
        left_button = (mouse_packet[0] & 0x01);
        int x_rel = (int)mouse_packet[1];
        int y_rel = (int)mouse_packet[2];
        if (mouse_packet[0] & 0x10) x_rel |= 0xFFFFFF00;
        if (mouse_packet[0] & 0x20) y_rel |= 0xFFFFFF00;
        mouse_x += x_rel;
        mouse_y -= y_rel;
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > (int)vbe_get_width()) mouse_x = vbe_get_width(); 
        if (mouse_y > (int)vbe_get_height()) mouse_y = vbe_get_height(); 
    }
    outb(0x20, 0x20);
    outb(0xA0, 0x20);
}
int get_mouse_x() { return mouse_x; }
int get_mouse_y() { return mouse_y; }
int mouse_left_pressed() { return left_button; }
