#include <stdint.h>
#include "string.h"
#include "net.h"
#include "usermode.h"
#include "user_abi.h"
#include "user_net_apps.h"

#define USER_CODE __attribute__((section(".user_code")))
#define USER_RODATA __attribute__((section(".user_rodata")))

#include "user_string.h"

#define strlen user_strlen
#define memset user_memset
#define USER_HTTP_IDLE_TIMEOUT 300U
#define USER_HTTP_TOTAL_TIMEOUT 1200U

static const char user_http_get_prefix[] USER_RODATA = "GET ";
static const char user_http_host_prefix[] USER_RODATA = " HTTP/1.0\r\nHost: ";
static const char user_http_headers[] USER_RODATA =
    "\r\nUser-Agent: NarcOs-Ring3/0.1\r\nConnection: close\r\n\r\n";
static const char user_netdemo_msg_start[] USER_RODATA = "Ring3 netdemo: HTTP request starting";
static const char user_netdemo_msg_ok[] USER_RODATA = "Ring3 netdemo: response";
static const char user_netdemo_msg_empty[] USER_RODATA = "(empty response)";
static const char user_netdemo_msg_err[] USER_RODATA = "Ring3 netdemo: HTTP request failed";
static const char user_netdemo_msg_truncated[] USER_RODATA = "Ring3 netdemo: response truncated";
static const char user_netdemo_msg_partial[] USER_RODATA = "Ring3 netdemo: connection timed out before clean close";
static const char user_fetch_msg_start[] USER_RODATA = "Ring3 fetch: downloading";
static const char user_fetch_msg_write_err[] USER_RODATA = "Ring3 fetch: failed to write file";

static USER_CODE int user_append_text(char* dst, uint16_t dst_len, uint16_t* io_offset, const char* src) {
    uint16_t off = *io_offset;

    if (!dst || !io_offset || !src || dst_len == 0U) return NET_ERR_INVALID;
    while (*src) {
        if (off + 1U >= dst_len) return NET_ERR_OVERFLOW;
        dst[off++] = *src++;
    }
    dst[off] = '\0';
    *io_offset = off;
    return NET_OK;
}

int USER_CODE user_http_fetch_text(const char* host, const char* path,
                                   char* response, uint16_t response_cap,
                                   net_http_result_t* out_result) {
    char request[512];
    char discard[128];
    uint16_t request_off = 0;
    uint16_t request_len;
    uint16_t response_off = 0;
    uint32_t server_ip = 0;
    uint32_t started;
    uint32_t last_progress;
    int socket_handle;
    int status = NET_OK;

    if (!host || !path || !response || response_cap < 2U || !out_result) return NET_ERR_INVALID;

    memset(out_result, 0, sizeof(*out_result));
    response[0] = '\0';

    if (user_net_resolve(host, &server_ip) != NET_OK) return NET_ERR_RESOLVE;
    out_result->resolved_ip = server_ip;

    request[0] = '\0';
    if (user_append_text(request, sizeof(request), &request_off, user_http_get_prefix) != NET_OK ||
        user_append_text(request, sizeof(request), &request_off, path) != NET_OK ||
        user_append_text(request, sizeof(request), &request_off, user_http_host_prefix) != NET_OK ||
        user_append_text(request, sizeof(request), &request_off, host) != NET_OK ||
        user_append_text(request, sizeof(request), &request_off, user_http_headers) != NET_OK) {
        return NET_ERR_OVERFLOW;
    }
    request_len = (uint16_t)strlen(request);

    socket_handle = user_net_socket(NET_SOCK_STREAM);
    if (socket_handle < 0) return socket_handle;

    status = user_net_connect(socket_handle, server_ip, 80, NET_TIMEOUT_CONNECT_DEFAULT);
    if (status < 0) {
        (void)user_net_close(socket_handle);
        return status;
    }

    status = user_net_send(socket_handle, request, request_len);
    if (status < 0) {
        (void)user_net_close(socket_handle);
        return status;
    }
    if ((uint16_t)status != request_len) {
        (void)user_net_close(socket_handle);
        return NET_ERR_IO;
    }

    started = user_uptime_ticks();
    last_progress = started;

    while ((user_uptime_ticks() - started) <= USER_HTTP_TOTAL_TIMEOUT) {
        int available = user_net_available(socket_handle);

        if (available == NET_ERR_CLOSED) {
            out_result->complete = 1U;
            break;
        }
        if (available < 0) {
            if (response_off == 0U) {
                (void)user_net_close(socket_handle);
                return available;
            }
            break;
        }
        if (available > 0) {
            int capacity = (int)(response_cap - 1U - response_off);
            void* target_buf = response + response_off;
            int to_read = available;
            int got;

            if (capacity <= 0) {
                target_buf = discard;
                to_read = available < (int)sizeof(discard) ? available : (int)sizeof(discard);
                out_result->truncated = 1U;
            } else if (to_read > capacity) {
                to_read = capacity;
                out_result->truncated = 1U;
            }

            got = user_net_recv(socket_handle, target_buf, (uint16_t)to_read);
            if (got < 0) {
                if (response_off == 0U) {
                    (void)user_net_close(socket_handle);
                    return got;
                }
                break;
            }
            if (got > 0) {
                if (target_buf == (void*)(response + response_off)) {
                    response_off = (uint16_t)(response_off + (uint16_t)got);
                }
                last_progress = user_uptime_ticks();
            }
        }

        if (user_uptime_ticks() > last_progress + USER_HTTP_IDLE_TIMEOUT) {
            break;
        }
        user_yield();
    }

    response[response_off] = '\0';
    out_result->response_len = response_off;
    status = user_net_close(socket_handle);
    if (status < 0 && status != NET_ERR_TIMEOUT && response_off == 0U) return status;
    return response_off != 0U ? NET_OK : NET_ERR_TIMEOUT;
}

uint32_t USER_CODE user_http_find_body(const char* response, uint32_t length) {
    for (uint32_t i = 0; i + 3U < length; i++) {
        if (response[i] == '\r' && response[i + 1U] == '\n' &&
            response[i + 2U] == '\r' && response[i + 3U] == '\n') {
            return i + 4U;
        }
    }
    for (uint32_t i = 0; i + 1U < length; i++) {
        if (response[i] == '\n' && response[i + 1U] == '\n') return i + 2U;
    }
    return 0;
}

void USER_CODE user_netdemo_entry_c(user_netdemo_state_t* state) {
    int status;

    if (!state) return;

    user_print(user_netdemo_msg_start);
    status = user_http_fetch_text(state->host, state->path,
                                  state->response, sizeof(state->response),
                                  &state->result);
    state->status = status;
    if (status < 0) {
        user_print(user_netdemo_msg_err);
        return;
    }

    user_print(user_netdemo_msg_ok);
    if (state->response[0] != '\0') user_print(state->response);
    else user_print(user_netdemo_msg_empty);
    if (state->result.truncated != 0U) user_print(user_netdemo_msg_truncated);
    if (state->result.complete == 0U) user_print(user_netdemo_msg_partial);
}

void USER_CODE user_fetch_entry_c(user_fetch_state_t* state) {
    int status;

    if (!state) return;

    user_print(user_fetch_msg_start);
    status = user_http_fetch_text(state->host, state->path,
                                  state->response, sizeof(state->response),
                                  &state->result);
    if (status < 0) {
        state->status = status;
        return;
    }

    state->body_offset = user_http_find_body(state->response, state->result.response_len);
    state->saved_len = (uint32_t)strlen(state->response + state->body_offset);
    if (user_fs_write(state->output_path, state->response + state->body_offset) != 0) {
        state->status = NET_ERR_IO;
        user_print(user_fetch_msg_write_err);
        return;
    }
    state->status = USER_APP_STATUS_OK;
}
