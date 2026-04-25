#include "net.h"
#include "string.h"

void vbe_compose_scene_basic(void) {}

int kernel_run_privileged_command(int cmd, const char* arg) {
    (void)cmd;
    (void)arg;
    return -1;
}

int kernel_gui_open_narcpad_file(const char* path) {
    (void)path;
    return -1;
}

int net_get_ipv4_config(net_ipv4_config_t* out_config) {
    if (out_config) memset(out_config, 0, sizeof(*out_config));
    return NET_ERR_NOT_READY;
}

int net_run_dhcp(int verbose) {
    (void)verbose;
    return NET_ERR_NOT_READY;
}

int net_ping_host(const char* target, net_ping_result_t* out_result) {
    (void)target;
    if (out_result) memset(out_result, 0, sizeof(*out_result));
    return NET_ERR_NOT_READY;
}

int net_socket_close(int handle) {
    (void)handle;
    return NET_ERR_NOT_READY;
}

int net_socket_connect(int handle, uint32_t remote_ip, uint16_t port, uint32_t timeout_ticks) {
    (void)handle;
    (void)remote_ip;
    (void)port;
    (void)timeout_ticks;
    return NET_ERR_NOT_READY;
}

int net_socket_open(int type) {
    (void)type;
    return NET_ERR_NOT_READY;
}

int net_ntp_query(const char* host, uint32_t* out_unix_seconds) {
    (void)host;
    if (out_unix_seconds) *out_unix_seconds = 0U;
    return NET_ERR_NOT_READY;
}

int net_resolve_ipv4(const char* host, uint32_t* out_ip) {
    (void)host;
    if (out_ip) *out_ip = 0U;
    return NET_ERR_NOT_READY;
}

int net_socket_recv(int handle, void* data, uint16_t length) {
    (void)handle;
    (void)data;
    (void)length;
    return NET_ERR_NOT_READY;
}

int net_socket_send(int handle, const void* data, uint16_t length) {
    (void)handle;
    (void)data;
    (void)length;
    return NET_ERR_NOT_READY;
}

int net_socket_available(int handle) {
    (void)handle;
    return NET_ERR_NOT_READY;
}
