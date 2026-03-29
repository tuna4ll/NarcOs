#ifndef RTC_H
#define RTC_H
#include <stdint.h>
#include <stddef.h>
void read_rtc();
void rtc_init_timezone();
int rtc_get_timezone_offset_minutes();
void rtc_set_timezone_offset_minutes(int minutes);
void rtc_adjust_timezone_offset_minutes(int delta_minutes);
int rtc_save_timezone_setting();
void rtc_format_timezone(char* buffer, size_t max_len);
uint8_t get_year();
uint8_t get_month();
uint8_t get_day();
uint8_t get_hour();
uint8_t get_minute();
uint8_t get_second();
#endif
