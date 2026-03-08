#include "rtc.h"

extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);

static uint8_t second;
static uint8_t minute;
static uint8_t hour;
static uint8_t day;
static uint8_t month;
static uint8_t year;

static int get_update_in_progress_flag() {
    outb(0x70, 0x0A);
    return (inb(0x71) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return ((bcd / 16) * 10) + (bcd & 0x0F);
}

void read_rtc() {
    uint8_t last_second;
    uint8_t last_minute;
    uint8_t last_hour;
    uint8_t last_day;
    uint8_t last_month;
    uint8_t last_year;
    uint8_t registerB;

    while (get_update_in_progress_flag());

    second = get_rtc_register(0x00);
    minute = get_rtc_register(0x02);
    hour = get_rtc_register(0x04);
    day = get_rtc_register(0x07);
    month = get_rtc_register(0x08);
    year = get_rtc_register(0x09);

    do {
        last_second = second;
        last_minute = minute;
        last_hour = hour;
        last_day = day;
        last_month = month;
        last_year = year;

        while (get_update_in_progress_flag());

        second = get_rtc_register(0x00);
        minute = get_rtc_register(0x02);
        hour = get_rtc_register(0x04);
        day = get_rtc_register(0x07);
        month = get_rtc_register(0x08);
        year = get_rtc_register(0x09);
    } while ((last_second != second) || (last_minute != minute) || (last_hour != hour) ||
             (last_day != day) || (last_month != month) || (last_year != year));

    registerB = get_rtc_register(0x0B);

    if (!(registerB & 0x04)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = ((hour & 0x0F) + (((hour & 0x70) / 16) * 10)) | (hour & 0x80);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
    }

    if (!(registerB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }
}

uint8_t get_year() { return year; }
uint8_t get_month() { return month; }
uint8_t get_day() { return day; }
uint8_t get_hour() { return hour; }
uint8_t get_minute() { return minute; }
uint8_t get_second() { return second; }
