#include "net_internal.h"

net_state_t net_state;
arp_entry_t arp_cache[ARP_CACHE_SIZE];
uint8_t rtl_rx_buffer[RTL_RX_ALLOC_SIZE] __attribute__((aligned(256)));
uint8_t rtl_tx_buffers[RTL_TX_BUFFER_COUNT][RTL_TX_BUFFER_SIZE] __attribute__((aligned(16)));

uint32_t net_secret[4] = {
    0x51F15EEDu, 0x6D2B79F5u, 0x9E3779B9u, 0xA5A5A5A5u
};
uint32_t net_prng_counter = 0;
uint32_t net_isn_last_tick = 0;
uint16_t net_isn_tick_offset = 0;
int net_secret_ready = 0;
uint32_t dhcp_xid = 0;
dhcp_offer_t dhcp_offer_state;
dhcp_offer_t dhcp_ack_state;
uint16_t pending_dns_id = 0;
uint16_t pending_dns_port = 0;
int pending_dns_status = -1;
uint32_t pending_dns_result_ip = 0;
uint16_t pending_ping_identifier = 0;
uint16_t pending_ping_sequence = 0;
uint32_t pending_ping_ip = 0;
uint32_t pending_ping_started = 0;
int pending_ping_status = -1;
uint32_t pending_ping_rtt_ms = 0;
uint16_t pending_udp_local_port = 0;
uint16_t pending_udp_remote_port = 0;
uint32_t pending_udp_remote_ip = 0;
int pending_udp_status = -1;
uint8_t* pending_udp_response_buf = 0;
uint16_t pending_udp_response_buf_len = 0;
uint16_t pending_udp_response_len = 0;
uint32_t pending_udp_response_ip = 0;
uint16_t pending_udp_response_port = 0;
net_socket_t net_sockets[NET_SOCKET_COUNT];
uint32_t net_last_ui_tick = 0;

uint16_t net_swap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

uint32_t net_swap32(uint32_t v) {
    return ((v & 0x000000FFU) << 24) |
           ((v & 0x0000FF00U) << 8) |
           ((v & 0x00FF0000U) >> 8) |
           ((v & 0xFF000000U) >> 24);
}

uint16_t net_htons(uint16_t v) { return net_swap16(v); }
uint16_t net_ntohs(uint16_t v) { return net_swap16(v); }
uint32_t net_htonl(uint32_t v) { return net_swap32(v); }
uint32_t net_ntohl(uint32_t v) { return net_swap32(v); }

static uint32_t net_rotl32(uint32_t value, uint8_t shift) {
    return (uint32_t)((value << shift) | (value >> (32U - shift)));
}

void net_write16_le(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
}

void net_write32_le(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

uint32_t net_read32_le(const uint8_t* src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static uint32_t net_read_tsc32() {
    const cpu_info_t* cpu = cpu_get_info();
    uint32_t lo = 0;
    uint32_t hi = 0;

    if (!cpu || !cpu->tsc_supported) return 0;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ hi;
}

static void net_md5_small(const uint8_t* data, uint32_t length, uint8_t out[16]) {
    static const uint32_t k[64] = {
        0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU,
        0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U,
        0x698098d8U, 0x8b44f7afU, 0xffff5bb1U, 0x895cd7beU,
        0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U,
        0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU,
        0xd62f105dU, 0x02441453U, 0xd8a1e681U, 0xe7d3fbc8U,
        0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU,
        0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU,
        0xfffa3942U, 0x8771f681U, 0x6d9d6122U, 0xfde5380cU,
        0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U,
        0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
        0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U,
        0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U,
        0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU, 0x85845dd1U,
        0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U,
        0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U
    };
    static const uint8_t s[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
    };
    uint8_t block[64];
    uint32_t words[16];
    uint32_t a = 0x67452301U;
    uint32_t b = 0xefcdab89U;
    uint32_t c = 0x98badcfeU;
    uint32_t d = 0x10325476U;

    if (length > 55U) length = 55U;

    memset(block, 0, sizeof(block));
    if (length != 0U) memcpy(block, data, length);
    block[length] = 0x80U;

    for (uint32_t i = 0; i < 14U; i++) {
        words[i] = net_read32_le(block + (i * 4U));
    }
    words[14] = length * 8U;
    words[15] = 0U;

    for (uint32_t i = 0; i < 64U; i++) {
        uint32_t f;
        uint32_t g;
        uint32_t temp;

        if (i < 16U) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32U) {
            f = (d & b) | (~d & c);
            g = (5U * i + 1U) & 0x0FU;
        } else if (i < 48U) {
            f = b ^ c ^ d;
            g = (3U * i + 5U) & 0x0FU;
        } else {
            f = c ^ (b | ~d);
            g = (7U * i) & 0x0FU;
        }

        temp = d;
        d = c;
        c = b;
        b = b + net_rotl32(a + f + k[i] + words[g], s[i]);
        a = temp;
    }

    a += 0x67452301U;
    b += 0xefcdab89U;
    c += 0x98badcfeU;
    d += 0x10325476U;

    net_write32_le(out + 0, a);
    net_write32_le(out + 4, b);
    net_write32_le(out + 8, c);
    net_write32_le(out + 12, d);
}

static void net_secret_init() {
    uint8_t seed[40];
    uint8_t digest[16];
    uint32_t tsc = net_read_tsc32();
    uint32_t mix = timer_ticks ^ tsc ^ net_state.io_base ^ ((uint32_t)net_state.irq_line << 24);

    if (net_secret_ready) return;

    read_rtc();
    memset(seed, 0, sizeof(seed));
    net_write32_le(seed + 0, mix);
    net_write32_le(seed + 4, net_state.ip_addr);
    net_write32_le(seed + 8, net_state.gateway);
    net_write32_le(seed + 12, net_state.dns_server);
    memcpy(seed + 16, net_state.mac, sizeof(net_state.mac));
    seed[22] = get_year();
    seed[23] = get_month();
    seed[24] = get_day();
    seed[25] = get_hour();
    seed[26] = get_minute();
    seed[27] = get_second();
    net_write32_le(seed + 28, (uint32_t)(uintptr_t)&net_state);
    net_write32_le(seed + 32, (uint32_t)(uintptr_t)&net_secret);
    net_write32_le(seed + 36, (uint32_t)(uintptr_t)&net_secret_init);
    net_md5_small(seed, sizeof(seed), digest);
    for (uint32_t i = 0; i < 4U; i++) {
        net_secret[i] ^= net_read32_le(digest + (i * 4U));
    }
    net_prng_counter = net_read32_le(digest) ^ mix ^ 0xA511E9B3u;
    net_secret_ready = 1;
}

static uint32_t net_prf32(const uint8_t* data, uint16_t length, uint32_t salt) {
    uint8_t block[55];
    uint8_t digest[16];

    if (length > sizeof(block) - 20U) length = (uint16_t)(sizeof(block) - 20U);

    net_secret_init();
    memcpy(block, net_secret, sizeof(net_secret));
    net_write32_le(block + 16, salt);
    if (length != 0U) memcpy(block + 20, data, length);
    net_md5_small(block, (uint32_t)(20U + length), digest);
    return net_read32_le(digest);
}

uint32_t net_random() {
    uint8_t data[8];

    net_write32_le(data + 0, timer_ticks);
    net_write32_le(data + 4, net_read_tsc32());
    return net_prf32(data, sizeof(data), net_prng_counter++);
}

uint16_t net_random16() {
    uint32_t value = net_random();
    return (uint16_t)((value >> 16) ^ value);
}

static uint32_t net_tcp_isn_time_component() {
    uint32_t ticks = timer_ticks;

    if (ticks != net_isn_last_tick) {
        net_isn_last_tick = ticks;
        net_isn_tick_offset = 0;
    } else if (net_isn_tick_offset != 0xFFFFU) {
        net_isn_tick_offset++;
    }
    return ticks * NET_TCP_ISN_TICK_INCREMENT + (uint32_t)net_isn_tick_offset;
}

uint32_t net_tcp_initial_sequence(uint32_t local_ip, uint16_t local_port,
                                  uint32_t remote_ip, uint16_t remote_port) {
    uint8_t tuple[12];

    net_write32_le(tuple + 0, local_ip);
    net_write16_le(tuple + 4, local_port);
    net_write32_le(tuple + 6, remote_ip);
    net_write16_le(tuple + 10, remote_port);
    return net_tcp_isn_time_component() + net_prf32(tuple, sizeof(tuple), 0x49534E31U);
}

uint16_t net_pick_ephemeral_port(uint16_t base, uint16_t count,
                                 uint32_t remote_ip, uint16_t remote_port,
                                 uint32_t salt) {
    uint8_t input[12];

    net_write32_le(input + 0, net_state.ip_addr);
    net_write32_le(input + 4, remote_ip);
    net_write16_le(input + 8, remote_port);
    net_write16_le(input + 10, base);
    return (uint16_t)(base + (net_prf32(input, sizeof(input), salt) % count));
}

void net_pump_ui() {
    process_t* current;
    if (timer_ticks == net_last_ui_tick) return;
    net_last_ui_tick = timer_ticks;
    current = process_current();
    if (current && strcmp(current->name, "desktop") == 0) {
        vbe_compose_scene_basic();
    } else {
        gui_needs_redraw = 1;
    }
}

void net_write16_be(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)((value >> 8) & 0xFF);
    dst[1] = (uint8_t)(value & 0xFF);
}

void net_write32_be(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)((value >> 24) & 0xFF);
    dst[1] = (uint8_t)((value >> 16) & 0xFF);
    dst[2] = (uint8_t)((value >> 8) & 0xFF);
    dst[3] = (uint8_t)(value & 0xFF);
}

uint32_t net_read32_be(const uint8_t* src) {
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static uint32_t net_checksum_partial(uint32_t sum, const void* data, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)data;

    while (length > 1) {
        sum += ((uint32_t)bytes[0] << 8) | (uint32_t)bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length != 0U) sum += (uint32_t)bytes[0] << 8;
    return sum;
}

static uint16_t net_checksum_finish(uint32_t sum) {
    while ((sum >> 16) != 0U) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
}

uint16_t net_checksum16(const void* data, uint32_t length) {
    return net_checksum_finish(net_checksum_partial(0, data, length));
}

uint16_t net_transport_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
                                const void* segment, uint16_t segment_len) {
    uint32_t sum = 0;

    sum += (src_ip >> 16) & 0xFFFFU;
    sum += src_ip & 0xFFFFU;
    sum += (dst_ip >> 16) & 0xFFFFU;
    sum += dst_ip & 0xFFFFU;
    sum += (uint16_t)protocol;
    sum += segment_len;
    sum = net_checksum_partial(sum, segment, segment_len);
    return net_checksum_finish(sum);
}

void net_print_hex_byte(uint8_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[3];

    buf[0] = digits[(value >> 4) & 0x0F];
    buf[1] = digits[value & 0x0F];
    buf[2] = '\0';
    vga_print(buf);
}

void net_print_ip(uint32_t ip) {
    vga_print_int((int)((ip >> 24) & 0xFF));
    vga_putchar('.');
    vga_print_int((int)((ip >> 16) & 0xFF));
    vga_putchar('.');
    vga_print_int((int)((ip >> 8) & 0xFF));
    vga_putchar('.');
    vga_print_int((int)(ip & 0xFF));
}

static void net_print_two_digits(uint32_t value) {
    if (value < 10U) vga_putchar('0');
    vga_print_int((int)value);
}

static int net_is_leap_year(uint32_t year) {
    if ((year % 4U) != 0U) return 0;
    if ((year % 100U) != 0U) return 1;
    return (year % 400U) == 0U;
}

static uint32_t net_days_in_month(uint32_t year, uint32_t month) {
    static const uint8_t days_per_month[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month == 2U && net_is_leap_year(year)) return 29U;
    if (month >= 1U && month <= 12U) return (uint32_t)days_per_month[month - 1U];
    return 30U;
}

void net_print_unix_utc(uint32_t unix_seconds) {
    uint32_t days = unix_seconds / 86400U;
    uint32_t seconds_of_day = unix_seconds % 86400U;
    uint32_t year = 1970U;
    uint32_t month = 1U;
    uint32_t day;
    uint32_t hour = seconds_of_day / 3600U;
    uint32_t minute = (seconds_of_day % 3600U) / 60U;
    uint32_t second = seconds_of_day % 60U;

    while (days >= (uint32_t)(net_is_leap_year(year) ? 366U : 365U)) {
        days -= (uint32_t)(net_is_leap_year(year) ? 366U : 365U);
        year++;
    }

    while (days >= net_days_in_month(year, month)) {
        days -= net_days_in_month(year, month);
        month++;
    }
    day = days + 1U;

    vga_print_int((int)year);
    vga_putchar('-');
    net_print_two_digits(month);
    vga_putchar('-');
    net_print_two_digits(day);
    vga_print(" ");
    net_print_two_digits(hour);
    vga_putchar(':');
    net_print_two_digits(minute);
    vga_putchar(':');
    net_print_two_digits(second);
    vga_print(" UTC");
}

int net_append_text(char* dst, uint16_t dst_len, uint16_t* io_offset, const char* src) {
    uint16_t off = *io_offset;

    if (!dst || !io_offset || !src || dst_len == 0U) return -1;
    while (*src) {
        if (off + 1U >= dst_len) return -1;
        dst[off++] = *src++;
    }
    dst[off] = '\0';
    *io_offset = off;
    return 0;
}

const char* net_strerror(int code) {
    switch (code) {
        case NET_OK: return "ok";
        case NET_ERR_INVALID: return "invalid argument";
        case NET_ERR_UNSUPPORTED: return "unsupported";
        case NET_ERR_NOT_READY: return "network not ready";
        case NET_ERR_NO_SOCKETS: return "no free sockets";
        case NET_ERR_RESOLVE: return "name resolution failed";
        case NET_ERR_TIMEOUT: return "timed out";
        case NET_ERR_STATE: return "invalid socket state";
        case NET_ERR_RESET: return "connection reset";
        case NET_ERR_CLOSED: return "connection closed";
        case NET_ERR_WOULD_BLOCK: return "would block";
        case NET_ERR_IO: return "i/o failure";
        case NET_ERR_OVERFLOW: return "buffer overflow";
        default: return "network error";
    }
}

void net_print_mac(const uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        net_print_hex_byte(mac[i]);
        if (i != 5) vga_putchar(':');
    }
}

int net_parse_ipv4_text(const char* text, uint32_t* out_ip) {
    uint32_t parts[4] = {0};
    int part = 0;
    int has_digit = 0;

    if (!text || !out_ip) return -1;
    while (*text) {
        char c = *text++;
        if (c >= '0' && c <= '9') {
            parts[part] = parts[part] * 10U + (uint32_t)(c - '0');
            if (parts[part] > 255U) return -1;
            has_digit = 1;
        } else if (c == '.') {
            if (!has_digit || part >= 3) return -1;
            part++;
            has_digit = 0;
        } else {
            return -1;
        }
    }
    if (part != 3 || !has_digit) return -1;
    *out_ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3];
    return 0;
}

int net_is_available() {
    return net_state.present;
}

int net_is_configured() {
    return net_state.present && net_state.configured;
}

int net_get_stats(net_stats_t* out_stats) {
    if (!out_stats) return -1;
    out_stats->rx_bytes = net_state.rx_bytes;
    out_stats->tx_bytes = net_state.tx_bytes;
    out_stats->rx_packets = net_state.rx_packets;
    out_stats->tx_packets = net_state.tx_packets;
    out_stats->available = net_state.present;
    out_stats->configured = net_state.present && net_state.configured;
    return net_state.present ? 0 : -1;
}

int net_get_ipv4_config(net_ipv4_config_t* out_config) {
    if (!out_config) return -1;

    memset(out_config, 0, sizeof(*out_config));
    out_config->available = net_state.present;
    out_config->configured = net_state.present && net_state.configured;
    out_config->ip_addr = net_state.ip_addr;
    out_config->netmask = net_state.netmask;
    out_config->gateway = net_state.gateway;
    out_config->dns_server = net_state.dns_server;
    return net_state.present ? 0 : -1;
}

int net_resolve_ipv4(const char* host, uint32_t* out_ip) {
    if (!host || host[0] == '\0' || !out_ip) return -1;
    if (net_ensure_configured() != 0) return -1;
    if (net_parse_ipv4_text(host, out_ip) == 0) return 0;
    return net_dns_lookup(host, out_ip);
}

void net_print_status() {
    if (!net_state.present) {
        vga_print_color("Network: RTL8139 not found.\n", 0x0C);
        vga_println("Recommended QEMU args: -netdev user,id=n0 -device rtl8139,netdev=n0");
        return;
    }

    vga_print("Network Driver : RTL8139 @ IO 0x");
    net_print_hex_byte((uint8_t)((net_state.io_base >> 8) & 0xFF));
    net_print_hex_byte((uint8_t)(net_state.io_base & 0xFF));
    vga_println("");
    if (net_state.irq_pin != 0U) {
        vga_print("IRQ            : ");
        vga_print(pci_irq_pin_name(net_state.irq_pin));
        if (net_state.irq_line != 0xFFU) {
            vga_print(" -> ");
            vga_print_int(net_state.irq_line);
            vga_println(net_state.irq_enabled ? " (legacy PIC enabled)" : " (legacy PIC masked)");
        } else {
            vga_println(" -> unrouted");
        }
    }
    vga_print("MAC            : ");
    net_print_mac(net_state.mac);
    vga_println("");
    vga_print("Address Mode   : ");
    vga_println(net_state.used_qemu_fallback ? "QEMU fallback" : (net_state.configured ? "DHCP" : "Unconfigured"));
    if (net_state.configured) {
        vga_print("IP             : ");
        net_print_ip(net_state.ip_addr);
        vga_println("");
        vga_print("Netmask        : ");
        net_print_ip(net_state.netmask);
        vga_println("");
        vga_print("Gateway        : ");
        net_print_ip(net_state.gateway);
        vga_println("");
        vga_print("DNS            : ");
        net_print_ip(net_state.dns_server);
        vga_println("");
    }
}
