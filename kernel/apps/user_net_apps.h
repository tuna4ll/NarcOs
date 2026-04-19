#ifndef USER_NET_APPS_H
#define USER_NET_APPS_H

#include <stdint.h>
#include "net.h"

int user_http_fetch_text(const char* host, const char* path,
                         char* response, uint16_t response_cap,
                         net_http_result_t* out_result);
int user_https_fetch_text(const char* host, const char* path,
                          char* response, uint16_t response_cap,
                          net_http_result_t* out_result);
uint32_t user_http_find_body(const char* response, uint32_t length);

#endif
