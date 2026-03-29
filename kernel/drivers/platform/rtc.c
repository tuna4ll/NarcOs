#include "rtc.h"
#include "fs.h"
#include "string.h"
extern void outb(uint16_t port, uint8_t val);
extern uint8_t inb(uint16_t port);
static uint8_t second;
static uint8_t minute;
static uint8_t hour;
static uint8_t day;
static uint8_t month;
static uint8_t year;
static int timezone_offset_minutes = 0;
static const char* rtc_timezone_path = "/system/timezone.cfg";

static int rtc_clamp_timezone(int minutes) {
    if (minutes < -720) return -720;
    if (minutes > 840) return 840;
    return minutes;
}

static int rtc_is_leap_year(int full_year) {
    if ((full_year % 400) == 0) return 1;
    if ((full_year % 100) == 0) return 0;
    return (full_year % 4) == 0;
}

static int rtc_days_in_month(int full_year, int month_value) {
    static const uint8_t month_days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    if (month_value == 2 && rtc_is_leap_year(full_year)) return 29;
    if (month_value < 1 || month_value > 12) return 30;
    return month_days[month_value - 1];
}

static void rtc_apply_timezone(void) {
    int full_year = 2000 + (int)year;
    int month_value = (int)month;
    int day_value = (int)day;
    int minute_of_day = ((int)hour * 60) + (int)minute + timezone_offset_minutes;

    while (minute_of_day < 0) {
        minute_of_day += 1440;
        day_value--;
        if (day_value < 1) {
            month_value--;
            if (month_value < 1) {
                month_value = 12;
                full_year--;
            }
            day_value = rtc_days_in_month(full_year, month_value);
        }
    }

    while (minute_of_day >= 1440) {
        day_value++;
        minute_of_day -= 1440;
        if (day_value > rtc_days_in_month(full_year, month_value)) {
            day_value = 1;
            month_value++;
            if (month_value > 12) {
                month_value = 1;
                full_year++;
            }
        }
    }

    hour = (uint8_t)(minute_of_day / 60);
    minute = (uint8_t)(minute_of_day % 60);
    day = (uint8_t)day_value;
    month = (uint8_t)month_value;
    year = (uint8_t)(full_year % 100);
}

static int rtc_parse_timezone_minutes(const char* text, int* out_minutes) {
    int sign = 1;
    int value = 0;
    int seen_digit = 0;
    int i = 0;
    if (!text || !out_minutes) return -1;
    while (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r') i++;
    if (text[i] == '-') {
        sign = -1;
        i++;
    } else if (text[i] == '+') {
        i++;
    }
    while (text[i] >= '0' && text[i] <= '9') {
        seen_digit = 1;
        value = value * 10 + (text[i] - '0');
        i++;
    }
    while (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r') i++;
    if (!seen_digit || text[i] != '\0') return -1;
    *out_minutes = sign * value;
    return 0;
}

static void rtc_format_offset_minutes(char* buffer, size_t max_len, int minutes) {
    char temp[16];
    int pos = 0;
    unsigned int value;
    if (!buffer || max_len == 0) return;
    if (minutes < 0) {
        temp[pos++] = '-';
        value = (unsigned int)(-minutes);
    } else {
        value = (unsigned int)minutes;
    }
    if (value == 0) {
        temp[pos++] = '0';
    } else {
        char rev[12];
        int rev_len = 0;
        while (value > 0 && rev_len < (int)sizeof(rev)) {
            rev[rev_len++] = (char)('0' + (value % 10U));
            value /= 10U;
        }
        while (rev_len > 0 && pos < (int)sizeof(temp) - 1) {
            temp[pos++] = rev[--rev_len];
        }
    }
    temp[pos] = '\0';
    strncpy(buffer, temp, max_len - 1);
    buffer[max_len - 1] = '\0';
}
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
    rtc_apply_timezone();
}
void rtc_init_timezone() {
    char buffer[32];
    int parsed;
    if (fs_read_file(rtc_timezone_path, buffer, sizeof(buffer)) != 0) return;
    if (rtc_parse_timezone_minutes(buffer, &parsed) != 0) return;
    timezone_offset_minutes = rtc_clamp_timezone(parsed);
}
int rtc_get_timezone_offset_minutes() { return timezone_offset_minutes; }
void rtc_set_timezone_offset_minutes(int minutes) {
    timezone_offset_minutes = rtc_clamp_timezone(minutes);
    read_rtc();
}
void rtc_adjust_timezone_offset_minutes(int delta_minutes) {
    rtc_set_timezone_offset_minutes(timezone_offset_minutes + delta_minutes);
}
int rtc_save_timezone_setting() {
    char buffer[16];
    rtc_format_offset_minutes(buffer, sizeof(buffer), timezone_offset_minutes);
    return fs_write_file(rtc_timezone_path, buffer);
}
void rtc_format_timezone(char* buffer, size_t max_len) {
    int offset;
    int abs_offset;
    int hours_value;
    int minutes_value;
    char sign_char;
    if (!buffer || max_len < 10) {
        if (buffer && max_len > 0) buffer[0] = '\0';
        return;
    }
    offset = timezone_offset_minutes;
    sign_char = offset >= 0 ? '+' : '-';
    abs_offset = offset >= 0 ? offset : -offset;
    hours_value = abs_offset / 60;
    minutes_value = abs_offset % 60;
    buffer[0] = 'U';
    buffer[1] = 'T';
    buffer[2] = 'C';
    buffer[3] = sign_char;
    buffer[4] = (char)('0' + ((hours_value / 10) % 10));
    buffer[5] = (char)('0' + (hours_value % 10));
    buffer[6] = ':';
    buffer[7] = (char)('0' + ((minutes_value / 10) % 10));
    buffer[8] = (char)('0' + (minutes_value % 10));
    buffer[9] = '\0';
}
uint8_t get_year() { return year; }
uint8_t get_month() { return month; }
uint8_t get_day() { return day; }
uint8_t get_hour() { return hour; }
uint8_t get_minute() { return minute; }
uint8_t get_second() { return second; }
