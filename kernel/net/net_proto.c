#include "net_internal.h"

static int net_ntp_query_ip(uint32_t server_ip, uint32_t* out_unix_seconds) {
    uint8_t request[48];
    uint8_t response[64];
    net_udp_response_info_t info;
    uint32_t ntp_seconds;

    if (!out_unix_seconds) return -1;

    memset(request, 0, sizeof(request));
    request[0] = 0x1B;

    if (net_udp_exchange(server_ip, 0, 123, request, sizeof(request),
                         response, sizeof(response), &info, NET_TIMEOUT_LONG) != 0) {
        return -1;
    }
    if (info.length < 48U) return -1;

    ntp_seconds = net_read32_be(response + 40);
    if (ntp_seconds < 2208988800U) return -1;

    *out_unix_seconds = ntp_seconds - 2208988800U;
    return 0;
}

static int dhcp_option_u32(const uint8_t* options, uint16_t options_len, uint8_t key, uint32_t* out_value) {
    uint16_t off = 0;

    while (off < options_len) {
        uint8_t opt = options[off++];
        if (opt == 0xFF) break;
        if (opt == 0x00) continue;
        if (off >= options_len) break;
        uint8_t len = options[off++];
        if (off + len > options_len) break;
        if (opt == key && len >= 4U) {
            *out_value = net_read32_be(options + off);
            return 0;
        }
        off += len;
    }
    return -1;
}

static int dhcp_option_u8(const uint8_t* options, uint16_t options_len, uint8_t key, uint8_t* out_value) {
    uint16_t off = 0;

    while (off < options_len) {
        uint8_t opt = options[off++];
        if (opt == 0xFF) break;
        if (opt == 0x00) continue;
        if (off >= options_len) break;
        uint8_t len = options[off++];
        if (off + len > options_len) break;
        if (opt == key && len >= 1U) {
            *out_value = options[off];
            return 0;
        }
        off += len;
    }
    return -1;
}

static void dhcp_clear_state() {
    memset(&dhcp_offer_state, 0, sizeof(dhcp_offer_state));
    memset(&dhcp_ack_state, 0, sizeof(dhcp_ack_state));
}

static int net_send_dhcp_message(uint8_t message_type, uint32_t requested_ip, uint32_t server_id) {
    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t payload[300];
    uint8_t* options = payload + 240;
    uint16_t options_len = 0;

    memset(payload, 0, sizeof(payload));
    payload[0] = 1;
    payload[1] = 1;
    payload[2] = 6;
    payload[3] = 0;
    net_write32_be(payload + 4, dhcp_xid);
    net_write16_be(payload + 10, 0x8000);
    memcpy(payload + 28, net_state.mac, 6);
    net_write32_be(payload + 236, DHCP_MAGIC_COOKIE);

    options[options_len++] = 53;
    options[options_len++] = 1;
    options[options_len++] = message_type;

    options[options_len++] = 55;
    options[options_len++] = 4;
    options[options_len++] = 1;
    options[options_len++] = 3;
    options[options_len++] = 6;
    options[options_len++] = 54;

    if (requested_ip != 0U) {
        options[options_len++] = 50;
        options[options_len++] = 4;
        net_write32_be(options + options_len, requested_ip);
        options_len += 4;
    }
    if (server_id != 0U) {
        options[options_len++] = 54;
        options[options_len++] = 4;
        net_write32_be(options + options_len, server_id);
        options_len += 4;
    }

    options[options_len++] = 255;
    return net_send_udp(0, 0xFFFFFFFFU, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, payload, sizeof(payload), broadcast_mac);
}

static int dns_skip_name(const uint8_t* packet, uint16_t packet_len, uint16_t offset, uint16_t* out_offset) {
    uint16_t off = offset;
    int jumped = 0;
    uint16_t end_offset = offset;

    while (off < packet_len) {
        uint8_t len = packet[off];
        if (len == 0U) {
            off++;
            if (!jumped) end_offset = off;
            *out_offset = end_offset;
            return 0;
        }
        if ((len & 0xC0U) == 0xC0U) {
            if (off + 1U >= packet_len) return -1;
            off += 2;
            if (!jumped) end_offset = off;
            *out_offset = end_offset;
            return 0;
        }
        off++;
        if (off + len > packet_len) return -1;
        off = (uint16_t)(off + len);
        if (!jumped) end_offset = off;
    }
    return -1;
}

int net_dns_lookup(const char* host, uint32_t* out_ip) {
    uint8_t packet[512];
    dns_header_t* dns = (dns_header_t*)packet;
    uint16_t offset = sizeof(dns_header_t);
    const char* cursor = host;

    if (!net_state.configured || net_state.dns_server == 0U) return -1;

    memset(packet, 0, sizeof(packet));
    pending_dns_id = net_random16();
    pending_dns_port = net_pick_ephemeral_port(NET_EPHEMERAL_PORT_BASE_UDP, NET_EPHEMERAL_PORT_COUNT,
                                               net_state.dns_server, DNS_SERVER_PORT, net_prng_counter++);
    pending_dns_status = -1;
    pending_dns_result_ip = 0;

    dns->transaction_id = net_htons(pending_dns_id);
    dns->flags = net_htons(0x0100);
    dns->questions = net_htons(1);

    while (*cursor && offset < sizeof(packet) - 6U) {
        uint16_t label_start = offset++;
        uint8_t label_len = 0;

        while (*cursor && *cursor != '.') {
            packet[offset++] = (uint8_t)*cursor++;
            label_len++;
            if (offset >= sizeof(packet) - 6U || label_len > 63U) return -1;
        }
        packet[label_start] = label_len;
        if (*cursor == '.') cursor++;
    }
    packet[offset++] = 0;
    net_write16_be(packet + offset, 1);
    offset += 2;
    net_write16_be(packet + offset, 1);
    offset += 2;

    if (net_send_udp(net_state.ip_addr, net_state.dns_server, pending_dns_port, DNS_SERVER_PORT, packet, offset, 0) != 0) {
        pending_dns_status = 1;
        return -1;
    }

    if (net_wait_until(&pending_dns_status, NET_TIMEOUT_LONG) != 0 || pending_dns_status != 0) return -1;
    *out_ip = pending_dns_result_ip;
    return 0;
}

static void net_apply_qemu_fallback() {
    net_state.ip_addr = 0x0A00020FU;
    net_state.netmask = 0xFFFFFF00U;
    net_state.gateway = 0x0A000202U;
    net_state.dns_server = 0x0A000203U;
    net_state.dhcp_server = 0x0A000202U;
    net_state.configured = 1;
    net_state.used_qemu_fallback = 1;
}

int net_run_dhcp(int verbose) {
    uint32_t server_id = 0;
    int status = -1;

    if (!net_state.present) {
        if (verbose) vga_print_color("error: RTL8139 NIC not found.\n", 0x0C);
        return -1;
    }

    dhcp_clear_state();
    net_state.configured = 0;
    net_state.used_qemu_fallback = 0;
    net_state.ip_addr = 0;
    net_state.netmask = 0;
    net_state.gateway = 0;
    net_state.dns_server = 0;
    net_state.dhcp_server = 0;
    dhcp_xid = net_random();

    if (net_send_dhcp_message(1, 0, 0) == 0) {
        int offer_status = -1;
        uint32_t deadline = timer_ticks + NET_TIMEOUT_LONG;

        while (timer_ticks <= deadline) {
            rtl8139_poll_receive();
            net_pump_ui();
            if (dhcp_offer_state.valid) {
                offer_status = 0;
                break;
            }
            asm volatile("hlt");
        }
        if (offer_status == 0) {
            server_id = dhcp_offer_state.server_id != 0U ? dhcp_offer_state.server_id : dhcp_offer_state.gateway;
            if (net_send_dhcp_message(3, dhcp_offer_state.yiaddr, server_id) == 0) {
                int ack_status = -1;
                deadline = timer_ticks + NET_TIMEOUT_LONG;
                while (timer_ticks <= deadline) {
                    rtl8139_poll_receive();
                    net_pump_ui();
                    if (dhcp_ack_state.valid) {
                        ack_status = 0;
                        break;
                    }
                    asm volatile("hlt");
                }
                if (ack_status == 0) {
                    net_state.ip_addr = dhcp_ack_state.yiaddr;
                    net_state.netmask = dhcp_ack_state.subnet != 0U ? dhcp_ack_state.subnet : 0xFFFFFF00U;
                    net_state.gateway = dhcp_ack_state.gateway != 0U ? dhcp_ack_state.gateway : server_id;
                    net_state.dns_server = dhcp_ack_state.dns != 0U ? dhcp_ack_state.dns : net_state.gateway;
                    net_state.dhcp_server = dhcp_ack_state.server_id != 0U ? dhcp_ack_state.server_id : server_id;
                    net_state.configured = 1;
                    status = 0;
                }
            }
        }
    }

    if (status != 0) {
        net_apply_qemu_fallback();
        if (verbose) {
            vga_print_color("warning: DHCP timed out, using QEMU user-net defaults.\n", 0x0E);
        }
        return 1;
    }
    if (verbose) vga_print_color("DHCP lease acquired.\n", 0x0A);
    return 0;
}

void net_handle_dns_payload(const uint8_t* payload, uint16_t payload_len) {
    const dns_header_t* dns;
    uint16_t answers;
    uint16_t offset;

    if (payload_len < sizeof(dns_header_t)) return;
    dns = (const dns_header_t*)payload;
    if (net_ntohs(dns->transaction_id) != pending_dns_id) return;
    if ((net_ntohs(dns->flags) & 0x8000U) == 0U) return;

    answers = net_ntohs(dns->answers);
    offset = sizeof(dns_header_t);

    for (uint16_t i = 0; i < net_ntohs(dns->questions); i++) {
        if (dns_skip_name(payload, payload_len, offset, &offset) != 0) {
            pending_dns_status = 1;
            return;
        }
        if (offset + 4U > payload_len) {
            pending_dns_status = 1;
            return;
        }
        offset = (uint16_t)(offset + 4U);
    }

    for (uint16_t i = 0; i < answers; i++) {
        uint16_t answer_type;
        uint16_t answer_class;
        uint16_t rdlength;

        if (dns_skip_name(payload, payload_len, offset, &offset) != 0) break;
        if (offset + 10U > payload_len) break;

        answer_type = (uint16_t)(((uint16_t)payload[offset] << 8) | payload[offset + 1]);
        answer_class = (uint16_t)(((uint16_t)payload[offset + 2] << 8) | payload[offset + 3]);
        rdlength = (uint16_t)(((uint16_t)payload[offset + 8] << 8) | payload[offset + 9]);
        offset = (uint16_t)(offset + 10U);

        if (offset + rdlength > payload_len) break;
        if (answer_type == 1U && answer_class == 1U && rdlength == 4U) {
            pending_dns_result_ip = net_read32_be(payload + offset);
            pending_dns_status = 0;
            return;
        }
        offset = (uint16_t)(offset + rdlength);
    }

    pending_dns_status = 1;
}

void net_handle_dhcp_payload(const uint8_t* payload, uint16_t payload_len) {
    const uint8_t* options;
    uint16_t options_len;
    uint8_t msg_type = 0;
    uint32_t packet_xid;
    uint32_t yiaddr;
    dhcp_offer_t parsed;

    if (payload_len < 240U) return;
    packet_xid = net_read32_be(payload + 4);
    if (packet_xid != dhcp_xid) return;
    if (net_read32_be(payload + 236) != DHCP_MAGIC_COOKIE) return;

    yiaddr = net_read32_be(payload + 16);
    options = payload + 240;
    options_len = (uint16_t)(payload_len - 240U);
    if (dhcp_option_u8(options, options_len, 53, &msg_type) != 0) return;

    memset(&parsed, 0, sizeof(parsed));
    parsed.yiaddr = yiaddr;
    parsed.valid = 1;
    dhcp_option_u32(options, options_len, 1, &parsed.subnet);
    dhcp_option_u32(options, options_len, 3, &parsed.gateway);
    dhcp_option_u32(options, options_len, 6, &parsed.dns);
    dhcp_option_u32(options, options_len, 54, &parsed.server_id);

    if (msg_type == 2U) dhcp_offer_state = parsed;
    else if (msg_type == 5U) dhcp_ack_state = parsed;
}

void net_handle_icmp_payload(uint32_t src_ip, const uint8_t* payload, uint16_t payload_len) {
    const icmp_echo_header_t* icmp;
    uint16_t identifier;
    uint16_t sequence;

    if (payload_len < sizeof(icmp_echo_header_t)) return;
    icmp = (const icmp_echo_header_t*)payload;
    if (icmp->type != 0U || icmp->code != 0U) return;

    identifier = net_ntohs(icmp->identifier);
    sequence = net_ntohs(icmp->sequence);
    if (identifier == pending_ping_identifier && sequence == pending_ping_sequence && src_ip == pending_ping_ip) {
        pending_ping_rtt_ms = (timer_ticks - pending_ping_started) * 10U;
        pending_ping_status = 0;
    }
}

int net_udp_exchange(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                     const void* payload, uint16_t payload_len,
                     void* response_buf, uint16_t response_buf_len,
                     net_udp_response_info_t* out_info, uint32_t timeout_ticks) {
    uint16_t local_port = src_port;

    if ((payload_len != 0U && !payload) || !response_buf || response_buf_len == 0U || !out_info) return -1;
    if (net_ensure_configured() != 0) return -1;

    if (local_port == 0U) {
        local_port = net_pick_ephemeral_port(NET_EPHEMERAL_PORT_BASE_UDP, NET_EPHEMERAL_PORT_COUNT,
                                             dst_ip, dst_port, net_prng_counter++);
    }

    net_clear_pending_udp();
    pending_udp_local_port = local_port;
    pending_udp_remote_port = dst_port;
    pending_udp_remote_ip = dst_ip;
    pending_udp_status = -1;
    pending_udp_response_buf = (uint8_t*)response_buf;
    pending_udp_response_buf_len = response_buf_len;

    if (net_send_udp(net_state.ip_addr, dst_ip, local_port, dst_port, payload, payload_len, 0) != 0) {
        net_clear_pending_udp();
        return -1;
    }

    if (net_wait_until(&pending_udp_status, timeout_ticks != 0U ? timeout_ticks : NET_TIMEOUT_LONG) != 0 ||
        pending_udp_status != 0) {
        net_clear_pending_udp();
        return -1;
    }

    out_info->src_ip = pending_udp_response_ip;
    out_info->src_port = pending_udp_response_port;
    out_info->length = pending_udp_response_len;
    net_clear_pending_udp();
    return 0;
}

int net_ntp_query(const char* host, uint32_t* out_unix_seconds) {
    uint32_t server_ip;
    const char* target = (host && host[0] != '\0') ? host : "time.google.com";

    if (!out_unix_seconds) return -1;
    if (net_resolve_ipv4(target, &server_ip) != 0) return -1;
    return net_ntp_query_ip(server_ip, out_unix_seconds);
}

int net_dns_command(const char* host) {
    uint32_t ip;

    if (!host || host[0] == '\0') {
        vga_print_color("Usage: dns <host>\n", 0x0E);
        return -1;
    }
    if (net_resolve_ipv4(host, &ip) != 0) {
        vga_print_color("error: DNS lookup failed.\n", 0x0C);
        return -1;
    }
    vga_print(host);
    vga_print(" -> ");
    net_print_ip(ip);
    vga_println("");
    return 0;
}

int net_ping_host(const char* target, net_ping_result_t* out_result) {
    uint32_t ip;
    uint16_t identifier;
    int final_status = NET_OK;

    if (!target || target[0] == '\0' || !out_result) return NET_ERR_INVALID;
    memset(out_result, 0, sizeof(*out_result));
    out_result->attempts = 4U;
    for (int i = 0; i < 4; i++) out_result->reply_status[i] = NET_ERR_TIMEOUT;

    if (!net_state.present) return NET_ERR_NOT_READY;
    if (net_resolve_ipv4(target, &ip) != 0) return NET_ERR_RESOLVE;

    out_result->resolved_ip = ip;

    identifier = (uint16_t)(0xB000U | (net_random16() & 0x0FFFU));
    for (int i = 0; i < 4; i++) {
        pending_ping_identifier = identifier;
        pending_ping_sequence = net_state.next_ping_seq++;
        pending_ping_ip = ip;
        pending_ping_started = timer_ticks;
        pending_ping_status = -1;
        pending_ping_rtt_ms = 0;

        if (net_send_icmp_echo(ip, pending_ping_identifier, pending_ping_sequence) != 0) {
            out_result->reply_status[i] = NET_ERR_IO;
            return NET_ERR_IO;
        }
        if (net_wait_until(&pending_ping_status, NET_TIMEOUT_LONG) == 0 && pending_ping_status == 0) {
            out_result->reply_status[i] = NET_OK;
            out_result->rtt_ms[i] = pending_ping_rtt_ms;
            out_result->success_count++;
        } else {
            pending_ping_status = -1;
            out_result->reply_status[i] = NET_ERR_TIMEOUT;
            final_status = NET_ERR_TIMEOUT;
        }
    }
    return out_result->success_count != 0U ? NET_OK : final_status;
}

int net_ping_command(const char* target) {
    net_ping_result_t result;
    int status;

    if (!target || target[0] == '\0') {
        vga_print_color("Usage: ping <host>\n", 0x0E);
        return -1;
    }

    status = net_ping_host(target, &result);
    if (status == NET_ERR_NOT_READY) {
        vga_print_color("error: RTL8139 NIC not found. Start QEMU with rtl8139 enabled.\n", 0x0C);
        return status;
    }
    if (status == NET_ERR_RESOLVE) {
        vga_print_color("error: Failed to resolve target host.\n", 0x0C);
        return status;
    }
    if (status == NET_ERR_IO) {
        vga_print_color("error: Failed to transmit ICMP packet.\n", 0x0C);
        return status;
    }

    vga_print("Pinging ");
    vga_print(target);
    vga_print(" [");
    net_print_ip(result.resolved_ip);
    vga_println("] ...");

    for (uint32_t i = 0; i < result.attempts; i++) {
        if (result.reply_status[i] == NET_OK) {
            vga_print("Reply from ");
            net_print_ip(result.resolved_ip);
            vga_print(": time=");
            vga_print_int((int)result.rtt_ms[i]);
            vga_println("ms");
        } else {
            vga_println("Request timed out.");
        }
    }
    return status;
}

int net_ntp_command(const char* host) {
    uint32_t ip;
    uint32_t unix_seconds;
    const char* target = (host && host[0] != '\0') ? host : "time.google.com";

    if (!net_state.present) {
        vga_print_color("error: Network driver is not ready.\n", 0x0C);
        return -1;
    }
    if (net_resolve_ipv4(target, &ip) != 0) {
        vga_print_color("error: Failed to resolve NTP server.\n", 0x0C);
        return -1;
    }
    if (net_ntp_query_ip(ip, &unix_seconds) != 0) {
        vga_print_color("error: NTP query failed.\n", 0x0C);
        return -1;
    }

    vga_print("NTP server      : ");
    vga_print(target);
    vga_print(" [");
    net_print_ip(ip);
    vga_println("]");
    vga_print("UTC time        : ");
    net_print_unix_utc(unix_seconds);
    vga_println("");
    vga_print("Unix seconds    : ");
    vga_print_int((int)unix_seconds);
    vga_println("");
    return 0;
}
