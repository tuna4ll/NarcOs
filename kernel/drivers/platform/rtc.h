#ifndef RTC_H
#define RTC_H
#include <stdint.h>
void read_rtc();
uint8_t get_year();
uint8_t get_month();
uint8_t get_day();
uint8_t get_hour();
uint8_t get_minute();
uint8_t get_second();
#endif
