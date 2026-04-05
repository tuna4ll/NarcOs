#include "net_internal.h"

static int net_seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static int net_seq_gt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

static uint32_t net_tcp_sequence_advance(uint8_t flags, uint16_t payload_len) {
    uint32_t advance = payload_len;

    if ((flags & TCP_FLAG_SYN) != 0U) advance++;
    if ((flags & TCP_FLAG_FIN) != 0U) advance++;
    return advance;
}

static void net_socket_reset(net_socket_t* socket) {
    if (!socket) return;
    memset(socket, 0, sizeof(*socket));
    socket->state = NET_SOCKET_STATE_IDLE;
}

static net_socket_t* net_socket_from_handle(int handle) {
    if (handle <= 0 || handle > NET_SOCKET_COUNT) return 0;
    if (!net_sockets[handle - 1].used) return 0;
    return &net_sockets[handle - 1];
}

static net_socket_t* net_socket_find_tcp(uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    for (int i = 0; i < NET_SOCKET_COUNT; i++) {
        net_socket_t* socket = &net_sockets[i];

        if (socket->used &&
            socket->type == NET_SOCK_STREAM &&
            socket->remote_ip == src_ip &&
            socket->remote_port == src_port &&
            socket->local_port == dst_port) {
            return socket;
        }
    }
    return 0;
}

static int net_socket_local_port_in_use(uint16_t port) {
    for (int i = 0; i < NET_SOCKET_COUNT; i++) {
        if (net_sockets[i].used && net_sockets[i].local_port == port) return 1;
    }
    return 0;
}

static uint16_t net_socket_alloc_local_port(uint32_t remote_ip, uint16_t remote_port) {
    uint16_t base = net_pick_ephemeral_port(NET_EPHEMERAL_PORT_BASE_TCP, NET_EPHEMERAL_PORT_COUNT,
                                            remote_ip, remote_port, net_prng_counter++);

    for (uint16_t i = 0; i < NET_EPHEMERAL_PORT_COUNT; i++) {
        uint16_t port = (uint16_t)(NET_EPHEMERAL_PORT_BASE_TCP +
                                   ((base - NET_EPHEMERAL_PORT_BASE_TCP + i) % NET_EPHEMERAL_PORT_COUNT));
        if (!net_socket_local_port_in_use(port)) return port;
    }
    return 0;
}

static int net_socket_fail(net_socket_t* socket, int error) {
    if (!socket) return error;
    socket->last_error = error;
    socket->state = NET_SOCKET_STATE_ERROR;
    socket->tx_active = 0;
    return error;
}

static int net_socket_transmit(net_socket_t* socket, uint32_t sequence, uint8_t flags,
                               const void* payload, uint16_t payload_len) {
    uint32_t acknowledgment = ((flags & TCP_FLAG_SYN) != 0U) ? 0U : socket->recv_next;

    if (!socket || !socket->used) return NET_ERR_INVALID;
    if (net_send_tcp_segment(socket->remote_ip, socket->local_port, socket->remote_port,
                             sequence, acknowledgment, flags,
                             (uint16_t)TCP_WINDOW_SIZE, payload, payload_len) != 0) {
        return net_socket_fail(socket, NET_ERR_IO);
    }
    socket->tx_last_tick = timer_ticks;
    return NET_OK;
}

static int net_socket_retransmit(net_socket_t* socket) {
    if (!socket || !socket->used) return NET_ERR_INVALID;
    if (!socket->tx_active) return NET_OK;
    if (socket->tx_attempts >= NET_SOCKET_MAX_RETRIES) {
        return net_socket_fail(socket, NET_ERR_TIMEOUT);
    }
    socket->tx_attempts++;
    return net_socket_transmit(socket, socket->tx_sequence, socket->tx_flags,
                               socket->tx_buf, socket->tx_payload_len);
}

static int net_socket_queue_tx(net_socket_t* socket, uint8_t flags,
                               const void* payload, uint16_t payload_len) {
    if (!socket || !socket->used) return NET_ERR_INVALID;
    if (payload_len > NET_SOCKET_TX_BUFFER || (payload_len != 0U && !payload)) return NET_ERR_INVALID;
    if (socket->tx_active) return NET_ERR_STATE;

    if (payload_len != 0U) memcpy(socket->tx_buf, payload, payload_len);
    socket->tx_sequence = socket->send_next;
    socket->tx_payload_len = payload_len;
    socket->tx_flags = flags;
    socket->tx_active = 1;
    socket->tx_attempts = 0;
    socket->send_next += net_tcp_sequence_advance(flags, payload_len);
    return net_socket_retransmit(socket);
}

static int net_socket_append_rx(net_socket_t* socket, const uint8_t* data, uint16_t length) {
    uint16_t available;

    if (!socket || !socket->used || (!data && length != 0U)) return NET_ERR_INVALID;
    available = (uint16_t)(NET_SOCKET_RX_BUFFER - socket->rx_len);
    if (length > available) {
        socket->overflowed = 1;
        return net_socket_fail(socket, NET_ERR_OVERFLOW);
    }
    if (length != 0U) {
        memcpy(socket->rx_buf + socket->rx_len, data, length);
        socket->rx_len = (uint16_t)(socket->rx_len + length);
    }
    return NET_OK;
}

static int net_socket_service(net_socket_t* socket) {
    if (!socket || !socket->used) return NET_ERR_INVALID;

    rtl8139_poll_receive();
    net_pump_ui();

    if (socket->state == NET_SOCKET_STATE_ERROR) {
        return socket->last_error != 0 ? socket->last_error : NET_ERR_IO;
    }
    if (socket->tx_active && timer_ticks - socket->tx_last_tick >= NET_TIMEOUT_TCP_RETRY) {
        return net_socket_retransmit(socket);
    }
    return NET_OK;
}

static int net_socket_wait_connected_handle(net_socket_t* socket, uint32_t timeout_ticks) {
    uint32_t deadline = timer_ticks + (timeout_ticks != 0U ? timeout_ticks : NET_TIMEOUT_CONNECT_DEFAULT);

    while (timer_ticks <= deadline) {
        int status = net_socket_service(socket);
        if (socket->state == NET_SOCKET_STATE_ESTABLISHED) return NET_OK;
        if (status < 0) return status;
        asm volatile("hlt");
    }
    return net_socket_fail(socket, NET_ERR_TIMEOUT);
}

static int net_socket_wait_tx_clear(net_socket_t* socket, uint32_t timeout_ticks) {
    uint32_t deadline = timer_ticks + (timeout_ticks != 0U ? timeout_ticks : NET_TIMEOUT_IO_DEFAULT);

    while (timer_ticks <= deadline) {
        int status = net_socket_service(socket);
        if (status < 0) return status;
        if (!socket->tx_active) return NET_OK;
        asm volatile("hlt");
    }
    return net_socket_fail(socket, NET_ERR_TIMEOUT);
}

static int net_socket_wait_close_handle(net_socket_t* socket, uint32_t timeout_ticks) {
    uint32_t deadline = timer_ticks + (timeout_ticks != 0U ? timeout_ticks : NET_TIMEOUT_CLOSE_DEFAULT);

    while (timer_ticks <= deadline) {
        int status = net_socket_service(socket);
        if (status < 0) return status;
        if (socket->state == NET_SOCKET_STATE_CLOSED || (!socket->tx_active && socket->peer_closed)) {
            socket->state = NET_SOCKET_STATE_CLOSED;
            return NET_OK;
        }
        asm volatile("hlt");
    }
    return NET_ERR_TIMEOUT;
}

static int net_socket_begin_connect_ip(net_socket_t* socket, uint32_t remote_ip, uint16_t remote_port) {
    uint16_t local_port;

    if (!socket || !socket->used || socket->type != NET_SOCK_STREAM) return NET_ERR_INVALID;
    if (net_ensure_configured() != 0) return NET_ERR_NOT_READY;

    local_port = net_socket_alloc_local_port(remote_ip, remote_port);
    if (local_port == 0U) return NET_ERR_NO_SOCKETS;

    socket->state = NET_SOCKET_STATE_CONNECTING;
    socket->last_error = 0;
    socket->peer_closed = 0;
    socket->overflowed = 0;
    socket->remote_ip = remote_ip;
    socket->local_port = local_port;
    socket->remote_port = remote_port;
    socket->send_next = net_tcp_initial_sequence(net_state.ip_addr, local_port, remote_ip, remote_port);
    socket->recv_next = 0;
    socket->rx_len = 0;
    socket->tx_active = 0;
    socket->last_event_tick = timer_ticks;

    return net_socket_queue_tx(socket, TCP_FLAG_SYN, 0, 0);
}

static int net_http_get_ip(uint32_t server_ip, const char* host, const char* path,
                           char* out_response, uint16_t out_response_len,
                           uint16_t* out_actual_len, int* out_truncated, int* out_complete) {
    char request[512];
    uint16_t request_off = 0;
    uint16_t request_len;
    uint16_t response_off = 0;
    uint32_t started = timer_ticks;
    uint32_t last_progress = timer_ticks;
    int socket_handle;
    int socket_status;
    char discard[128];

    if (!host || !path || !out_response || out_response_len < 2U || !out_actual_len) return NET_ERR_INVALID;
    if (out_truncated) *out_truncated = 0;
    if (out_complete) *out_complete = 0;

    request[0] = '\0';
    if (net_append_text(request, sizeof(request), &request_off, "GET ") != 0 ||
        net_append_text(request, sizeof(request), &request_off, path) != 0 ||
        net_append_text(request, sizeof(request), &request_off, " HTTP/1.0\r\nHost: ") != 0 ||
        net_append_text(request, sizeof(request), &request_off, host) != 0 ||
        net_append_text(request, sizeof(request), &request_off,
                        "\r\nUser-Agent: NarcOs/0.1\r\nConnection: close\r\n\r\n") != 0) {
        return NET_ERR_OVERFLOW;
    }
    request_len = (uint16_t)strlen(request);

    socket_handle = net_socket_open(NET_SOCK_STREAM);
    if (socket_handle < 0) return socket_handle;
    socket_status = net_socket_connect(socket_handle, server_ip, 80, NET_TIMEOUT_LONG);
    if (socket_status != NET_OK) {
        (void)net_socket_close(socket_handle);
        return socket_status;
    }
    socket_status = net_socket_send(socket_handle, request, request_len);
    if (socket_status < 0) {
        (void)net_socket_close(socket_handle);
        return socket_status;
    }
    if ((uint16_t)socket_status != request_len) {
        (void)net_socket_close(socket_handle);
        return NET_ERR_IO;
    }

    while ((timer_ticks - started) <= NET_TIMEOUT_HTTP_TOTAL) {
        int available;

        rtl8139_poll_receive();
        net_pump_ui();
        available = net_socket_available(socket_handle);
        if (available == NET_ERR_CLOSED) {
            if (out_complete) *out_complete = 1;
            break;
        }
        if (available < 0) {
            (void)net_socket_close(socket_handle);
            return available;
        }
        if (available > 0) {
            int capacity = (int)(out_response_len - 1U - response_off);
            void* target_buf = out_response + response_off;
            int to_read = available;
            int got;

            if (capacity <= 0) {
                target_buf = discard;
                to_read = available < (int)sizeof(discard) ? available : (int)sizeof(discard);
                if (out_truncated) *out_truncated = 1;
            } else if (to_read > capacity) {
                to_read = capacity;
                if (out_truncated) *out_truncated = 1;
            }

            got = net_socket_recv(socket_handle, target_buf, (uint16_t)to_read);
            if (got < 0) {
                (void)net_socket_close(socket_handle);
                return got;
            }
            if (got > 0) {
                if (target_buf == (void*)(out_response + response_off)) {
                    response_off = (uint16_t)(response_off + (uint16_t)got);
                }
                last_progress = timer_ticks;
            }
        }

        if (timer_ticks > last_progress + NET_TIMEOUT_HTTP_IDLE) {
            if (out_complete) *out_complete = 0;
            break;
        }
        asm volatile("hlt");
    }

    out_response[response_off] = '\0';
    *out_actual_len = response_off;
    socket_status = net_socket_close(socket_handle);
    if (socket_status < 0 && socket_status != NET_ERR_TIMEOUT && response_off == 0U) return socket_status;
    return response_off != 0U ? NET_OK : NET_ERR_TIMEOUT;
}

void net_handle_tcp_payload(uint32_t src_ip, uint32_t dst_ip, const uint8_t* payload, uint16_t payload_len) {
    const tcp_header_t* tcp;
    net_socket_t* socket;
    uint16_t header_len;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t sequence;
    uint32_t acknowledgment;
    uint8_t flags;
    const uint8_t* tcp_payload;
    uint16_t tcp_payload_len;
    uint32_t tx_end;

    if (payload_len < sizeof(tcp_header_t)) return;
    if (!net_state.configured || dst_ip != net_state.ip_addr) return;

    tcp = (const tcp_header_t*)payload;
    header_len = (uint16_t)(((tcp->data_offset_reserved >> 4) & 0x0F) * 4U);
    if (header_len < sizeof(tcp_header_t) || payload_len < header_len) return;

    src_port = net_ntohs(tcp->src_port);
    dst_port = net_ntohs(tcp->dst_port);
    socket = net_socket_find_tcp(src_ip, src_port, dst_port);
    if (!socket) return;

    sequence = net_ntohl(tcp->sequence);
    acknowledgment = net_ntohl(tcp->acknowledgment);
    flags = tcp->flags;
    tcp_payload = payload + header_len;
    tcp_payload_len = (uint16_t)(payload_len - header_len);

    if ((flags & TCP_FLAG_RST) != 0U) {
        (void)net_socket_fail(socket, NET_ERR_RESET);
        return;
    }

    socket->last_event_tick = timer_ticks;

    if (socket->state == NET_SOCKET_STATE_CONNECTING) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK) &&
            acknowledgment == socket->send_next) {
            socket->recv_next = sequence + 1U;
            socket->tx_active = 0;
            socket->tx_payload_len = 0;
            socket->tx_attempts = 0;
            if (net_socket_transmit(socket, socket->send_next, TCP_FLAG_ACK, 0, 0) != NET_OK) {
                return;
            }
            socket->state = NET_SOCKET_STATE_ESTABLISHED;
        }
        return;
    }

    if ((flags & TCP_FLAG_ACK) != 0U) {
        if (net_seq_gt(acknowledgment, socket->send_next)) {
            (void)net_socket_fail(socket, NET_ERR_STATE);
            return;
        }
        if (socket->tx_active) {
            tx_end = socket->tx_sequence + net_tcp_sequence_advance(socket->tx_flags, socket->tx_payload_len);
            if (!net_seq_lt(acknowledgment, tx_end)) {
                socket->tx_active = 0;
                socket->tx_payload_len = 0;
                socket->tx_attempts = 0;
                if (socket->state == NET_SOCKET_STATE_CLOSING && socket->peer_closed) {
                    socket->state = NET_SOCKET_STATE_CLOSED;
                }
            }
        }
    }

    if (socket->state != NET_SOCKET_STATE_ESTABLISHED &&
        socket->state != NET_SOCKET_STATE_CLOSE_WAIT &&
        socket->state != NET_SOCKET_STATE_CLOSING) {
        return;
    }

    if ((tcp_payload_len != 0U || (flags & TCP_FLAG_FIN) != 0U) && sequence != socket->recv_next) {
        if ((flags & (TCP_FLAG_FIN | TCP_FLAG_SYN)) != 0U || tcp_payload_len != 0U) {
            (void)net_socket_transmit(socket, socket->send_next, TCP_FLAG_ACK, 0, 0);
        }
        return;
    }

    if (tcp_payload_len != 0U) {
        if (net_socket_append_rx(socket, tcp_payload, tcp_payload_len) != NET_OK) return;
        socket->recv_next += tcp_payload_len;
    }

    if ((flags & TCP_FLAG_FIN) != 0U) {
        socket->recv_next += 1U;
        socket->peer_closed = 1;
        if (socket->state == NET_SOCKET_STATE_ESTABLISHED) {
            socket->state = NET_SOCKET_STATE_CLOSE_WAIT;
        }
    }

    if (tcp_payload_len != 0U || (flags & TCP_FLAG_FIN) != 0U) {
        if (net_socket_transmit(socket, socket->send_next, TCP_FLAG_ACK, 0, 0) != NET_OK) return;
    }

    if (socket->state == NET_SOCKET_STATE_CLOSING && socket->peer_closed && !socket->tx_active) {
        socket->state = NET_SOCKET_STATE_CLOSED;
    }
}

int net_socket_open(int type) {
    if (type != NET_SOCK_STREAM) return NET_ERR_UNSUPPORTED;

    for (int i = 0; i < NET_SOCKET_COUNT; i++) {
        if (net_sockets[i].used) continue;
        net_socket_reset(&net_sockets[i]);
        net_sockets[i].used = 1;
        net_sockets[i].type = type;
        return i + 1;
    }
    return NET_ERR_NO_SOCKETS;
}

int net_socket_connect(int handle, uint32_t remote_ip, uint16_t port, uint32_t timeout_ticks) {
    net_socket_t* socket = net_socket_from_handle(handle);
    int status;

    if (!socket) return NET_ERR_INVALID;
    if (remote_ip == 0U || port == 0U) return NET_ERR_INVALID;
    if (socket->state != NET_SOCKET_STATE_IDLE && socket->state != NET_SOCKET_STATE_CLOSED) {
        return NET_ERR_STATE;
    }

    status = net_socket_begin_connect_ip(socket, remote_ip, port);
    if (status < 0) return status;
    return net_socket_wait_connected_handle(socket, timeout_ticks);
}

int net_socket_send(int handle, const void* data, uint16_t length) {
    net_socket_t* socket = net_socket_from_handle(handle);
    const uint8_t* bytes = (const uint8_t*)data;
    uint16_t sent = 0;

    if (!socket) return NET_ERR_INVALID;
    if (length == 0U) return 0;
    if (!data) return NET_ERR_INVALID;
    if (socket->state == NET_SOCKET_STATE_ERROR) return socket->last_error != 0 ? socket->last_error : NET_ERR_IO;
    if (socket->state != NET_SOCKET_STATE_ESTABLISHED) {
        return socket->peer_closed ? NET_ERR_CLOSED : NET_ERR_STATE;
    }
    if (socket->peer_closed) return NET_ERR_CLOSED;

    while (sent < length) {
        uint16_t chunk = (uint16_t)(length - sent);
        int status;

        if (chunk > NET_SOCKET_TX_BUFFER) chunk = NET_SOCKET_TX_BUFFER;
        status = net_socket_queue_tx(socket, TCP_FLAG_ACK | TCP_FLAG_PSH, bytes + sent, chunk);
        if (status < 0) return sent != 0U ? (int)sent : status;
        status = net_socket_wait_tx_clear(socket, NET_TIMEOUT_IO_DEFAULT);
        if (status < 0) return sent != 0U ? (int)sent : status;
        sent = (uint16_t)(sent + chunk);
    }

    return (int)sent;
}

int net_socket_recv(int handle, void* data, uint16_t length) {
    net_socket_t* socket = net_socket_from_handle(handle);
    uint8_t* dst = (uint8_t*)data;
    uint16_t copy_len;
    uint16_t remaining;

    if (!socket) return NET_ERR_INVALID;
    if (length == 0U) return 0;
    if (!data) return NET_ERR_INVALID;

    if (socket->rx_len == 0U) {
        int status = net_socket_service(socket);
        if (status < 0 && socket->rx_len == 0U) return status;
        if (socket->state == NET_SOCKET_STATE_ERROR) {
            return socket->last_error != 0 ? socket->last_error : NET_ERR_IO;
        }
        if (socket->peer_closed || socket->state == NET_SOCKET_STATE_CLOSED || socket->state == NET_SOCKET_STATE_CLOSE_WAIT) {
            return NET_ERR_CLOSED;
        }
        return NET_ERR_WOULD_BLOCK;
    }

    copy_len = length < socket->rx_len ? length : socket->rx_len;
    memcpy(dst, socket->rx_buf, copy_len);
    remaining = (uint16_t)(socket->rx_len - copy_len);
    for (uint16_t i = 0; i < remaining; i++) {
        socket->rx_buf[i] = socket->rx_buf[i + copy_len];
    }
    socket->rx_len = remaining;
    return (int)copy_len;
}

int net_socket_available(int handle) {
    net_socket_t* socket = net_socket_from_handle(handle);
    int status;

    if (!socket) return NET_ERR_INVALID;
    status = net_socket_service(socket);
    if (status < 0 && socket->rx_len == 0U) return status;
    if (socket->rx_len != 0U) return (int)socket->rx_len;
    if (socket->state == NET_SOCKET_STATE_ERROR) {
        return socket->last_error != 0 ? socket->last_error : NET_ERR_IO;
    }
    if (socket->peer_closed || socket->state == NET_SOCKET_STATE_CLOSED || socket->state == NET_SOCKET_STATE_CLOSE_WAIT) {
        return NET_ERR_CLOSED;
    }
    return 0;
}

int net_socket_close(int handle) {
    net_socket_t* socket = net_socket_from_handle(handle);
    int status = NET_OK;

    if (!socket) return NET_ERR_INVALID;

    if (socket->state == NET_SOCKET_STATE_ERROR) {
        status = socket->last_error != 0 ? socket->last_error : NET_ERR_IO;
        net_socket_reset(socket);
        return status;
    }
    if (socket->state == NET_SOCKET_STATE_IDLE || socket->state == NET_SOCKET_STATE_CLOSED) {
        net_socket_reset(socket);
        return NET_OK;
    }
    if (socket->state == NET_SOCKET_STATE_CONNECTING) {
        net_socket_reset(socket);
        return NET_ERR_STATE;
    }

    if (socket->state == NET_SOCKET_STATE_ESTABLISHED || socket->state == NET_SOCKET_STATE_CLOSE_WAIT) {
        socket->state = NET_SOCKET_STATE_CLOSING;
        status = net_socket_queue_tx(socket, TCP_FLAG_ACK | TCP_FLAG_FIN, 0, 0);
        if (status >= 0) status = net_socket_wait_close_handle(socket, NET_TIMEOUT_CLOSE_DEFAULT);
    } else if (socket->state == NET_SOCKET_STATE_CLOSING) {
        status = net_socket_wait_close_handle(socket, NET_TIMEOUT_CLOSE_DEFAULT);
    }

    net_socket_reset(socket);
    return status;
}

static int net_parse_http_target(const char* target, char* host, uint16_t host_len,
                                 char* path, uint16_t path_len) {
    const char* cursor = target;
    uint16_t host_off = 0;
    uint16_t path_off = 0;

    if (!target || !host || !path || host_len == 0U || path_len < 2U) return -1;
    while (*cursor == ' ') cursor++;
    if (strncmp(cursor, "http://", 7) == 0) cursor += 7;

    while (*cursor && *cursor != ' ' && *cursor != '/') {
        if (host_off + 1U >= host_len) return -1;
        host[host_off++] = *cursor++;
    }
    host[host_off] = '\0';
    if (host_off == 0U) return -1;

    if (*cursor == '/') {
        while (*cursor && *cursor != ' ') {
            if (path_off + 1U >= path_len) return -1;
            path[path_off++] = *cursor++;
        }
    } else {
        while (*cursor == ' ') cursor++;
        if (*cursor != '\0') {
            if (*cursor != '/') {
                if (path_off + 1U >= path_len) return -1;
                path[path_off++] = '/';
            }
            while (*cursor) {
                if (path_off + 1U >= path_len) return -1;
                path[path_off++] = *cursor++;
            }
        }
    }

    if (path_off == 0U) path[path_off++] = '/';
    path[path_off] = '\0';
    return 0;
}

static void net_sanitize_text_response(char* buffer, uint16_t* io_len) {
    uint16_t src = 0;
    uint16_t dst = 0;
    uint16_t len;

    if (!buffer || !io_len) return;
    len = *io_len;
    while (src < len) {
        char c = buffer[src++];
        if (c == '\r') continue;
        if (c == '\0') c = ' ';
        if (c == '\n' || c == '\t' || (c >= ' ' && c <= '~')) buffer[dst++] = c;
        else buffer[dst++] = '.';
    }
    buffer[dst] = '\0';
    *io_len = dst;
}

int net_http_fetch(const char* target, char* response, uint16_t response_buf_len,
                   net_http_result_t* out_result) {
    char host[128];
    char path[192];
    uint32_t ip;
    uint16_t response_len = 0;
    int truncated = 0;
    int complete = 0;
    int status;

    if (!target || target[0] == '\0' || !response || response_buf_len < 2U) return -1;
    if (out_result) memset(out_result, 0, sizeof(*out_result));
    if (net_parse_http_target(target, host, sizeof(host), path, sizeof(path)) != 0) return NET_ERR_INVALID;
    status = net_resolve_ipv4(host, &ip);
    if (status != NET_OK) return status == -1 ? NET_ERR_RESOLVE : status;
    status = net_http_get_ip(ip, host, path, response, response_buf_len, &response_len, &truncated, &complete);
    if (status != NET_OK) return status;

    net_sanitize_text_response(response, &response_len);
    if (out_result) {
        out_result->resolved_ip = ip;
        out_result->response_len = response_len;
        out_result->truncated = (uint32_t)truncated;
        out_result->complete = (uint32_t)complete;
    }
    return 0;
}

int net_http_command(const char* target) {
    static char response[4096];
    net_http_result_t result;
    int status;

    if (!target || target[0] == '\0') {
        vga_print_color("Usage: http <host> [path]\n", 0x0E);
        return -1;
    }
    status = net_http_fetch(target, response, sizeof(response), &result);
    if (status != NET_OK) {
        vga_print_color("error: HTTP request failed: ", 0x0C);
        vga_println(net_strerror(status));
        return status;
    }

    vga_print("HTTP GET        : ");
    vga_print(target);
    vga_println("");
    vga_print("Resolved        : ");
    net_print_ip(result.resolved_ip);
    vga_println("");
    vga_println("---- response ----");
    if (result.response_len != 0U) vga_println(response);
    else vga_println("(empty response)");
    if (result.truncated != 0U) {
        vga_print_color("warning: Response truncated to local buffer size.\n", 0x0E);
    }
    if (result.complete == 0U) {
        vga_print_color("warning: Remote peer did not close cleanly before timeout.\n", 0x0E);
    }
    return 0;
}
