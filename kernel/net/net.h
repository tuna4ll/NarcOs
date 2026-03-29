#ifndef NET_H
#define NET_H

#include <stdint.h>

#define NET_SOCK_STREAM 1

#define NET_OK 0
#define NET_ERR_INVALID -1
#define NET_ERR_UNSUPPORTED -2
#define NET_ERR_NOT_READY -3
#define NET_ERR_NO_SOCKETS -4
#define NET_ERR_RESOLVE -5
#define NET_ERR_TIMEOUT -6
#define NET_ERR_STATE -7
#define NET_ERR_RESET -8
#define NET_ERR_CLOSED -9
#define NET_ERR_WOULD_BLOCK -10
#define NET_ERR_IO -11
#define NET_ERR_OVERFLOW -12

#define NET_TIMEOUT_CONNECT_DEFAULT 300U
#define NET_TIMEOUT_IO_DEFAULT 300U
#define NET_TIMEOUT_CLOSE_DEFAULT 200U

typedef enum {
    NET_SOCKET_STATE_IDLE = 0,
    NET_SOCKET_STATE_CONNECTING,
    NET_SOCKET_STATE_ESTABLISHED,
    NET_SOCKET_STATE_CLOSE_WAIT,
    NET_SOCKET_STATE_CLOSING,
    NET_SOCKET_STATE_CLOSED,
    NET_SOCKET_STATE_ERROR
} net_socket_state_t;

typedef struct {
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    int available;
    int configured;
} net_ipv4_config_t;

typedef struct {
    uint32_t src_ip;
    uint16_t src_port;
    uint16_t length;
} net_udp_response_info_t;

typedef struct {
    uint32_t resolved_ip;
    uint32_t response_len;
    uint32_t truncated;
    uint32_t complete;
} net_http_result_t;

typedef struct {
    uint32_t resolved_ip;
    uint32_t attempts;
    uint32_t success_count;
    int32_t reply_status[4];
    uint32_t rtt_ms[4];
} net_ping_result_t;

void net_init();
void net_poll();
int net_is_available();
int net_is_configured();
int net_get_ipv4_config(net_ipv4_config_t* out_config);
int net_resolve_ipv4(const char* host, uint32_t* out_ip);
int net_udp_exchange(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                     const void* payload, uint16_t payload_len,
                     void* response_buf, uint16_t response_buf_len,
                     net_udp_response_info_t* out_info, uint32_t timeout_ticks);
int net_ntp_query(const char* host, uint32_t* out_unix_seconds);
const char* net_strerror(int code);
int net_socket_open(int type);
int net_socket_connect(int handle, uint32_t remote_ip, uint16_t port, uint32_t timeout_ticks);
int net_socket_send(int handle, const void* data, uint16_t length);
int net_socket_recv(int handle, void* data, uint16_t length);
int net_socket_available(int handle);
int net_socket_close(int handle);
int net_http_fetch(const char* target, char* response, uint16_t response_buf_len,
                   net_http_result_t* out_result);
int net_ping_host(const char* target, net_ping_result_t* out_result);
void net_print_status();
int net_run_dhcp(int verbose);
int net_dns_command(const char* host);
int net_ping_command(const char* target);
int net_ntp_command(const char* host);
int net_http_command(const char* target);

#endif
