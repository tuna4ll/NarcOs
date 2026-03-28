#include <stdint.h>
#include <stddef.h>
#include "net.h"
#include "io.h"
#include "string.h"

extern void vga_print(const char* str);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_putchar(char c);
extern volatile uint32_t timer_ticks;
extern void vbe_compose_scene_basic();
extern void vbe_update();

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

#define RTL_REG_IDR0      0x00
#define RTL_REG_TSD0      0x10
#define RTL_REG_TSAD0     0x20
#define RTL_REG_RBSTART   0x30
#define RTL_REG_CR        0x37
#define RTL_REG_CAPR      0x38
#define RTL_REG_CBR       0x3A
#define RTL_REG_IMR       0x3C
#define RTL_REG_ISR       0x3E
#define RTL_REG_TCR       0x40
#define RTL_REG_RCR       0x44
#define RTL_REG_CONFIG1   0x52

#define RTL_CR_RX_ENABLE  0x08
#define RTL_CR_TX_ENABLE  0x04
#define RTL_CR_RESET      0x10
#define RTL_CR_BUFE       0x01

#define RTL_ISR_ROK       0x0001
#define RTL_ISR_RER       0x0002
#define RTL_ISR_TOK       0x0004
#define RTL_ISR_TER       0x0008
#define RTL_ISR_RXOVW     0x0010
#define RTL_ISR_FIFOOVW   0x0040

#define RTL_TSD_TOK       0x00008000U
#define RTL_TSD_TABT      0x40000000U
#define RTL_TSD_TUN       0x00004000U

#define RTL_RX_BUFFER_SIZE 8192U
#define RTL_RX_ALLOC_SIZE  (RTL_RX_BUFFER_SIZE + 16U + 1500U)
#define RTL_TX_BUFFER_SIZE 1792U
#define RTL_TX_BUFFER_COUNT 4

#define ETH_TYPE_IPV4 0x0800
#define ETH_TYPE_ARP  0x0806

#define IPV4_PROTOCOL_TCP  6
#define IPV4_PROTOCOL_ICMP 1
#define IPV4_PROTOCOL_UDP  17

#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DNS_SERVER_PORT 53

#define DHCP_MAGIC_COOKIE 0x63825363U

#define ARP_CACHE_SIZE 8
#define NET_TIMEOUT_SHORT 100U
#define NET_TIMEOUT_MEDIUM 200U
#define NET_TIMEOUT_LONG 300U
#define NET_TIMEOUT_TCP_RETRY 50U
#define NET_TIMEOUT_HTTP_IDLE 300U
#define NET_TIMEOUT_HTTP_TOTAL 1200U

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_WINDOW_SIZE 4096U
#define NET_SOCKET_COUNT 4
#define NET_SOCKET_RX_BUFFER 4096U
#define NET_SOCKET_TX_BUFFER 1024U
#define NET_SOCKET_MAX_RETRIES 4U

typedef struct {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed)) eth_header_t;

typedef struct {
    uint16_t htype;
    uint16_t ptype;
    uint8_t hlen;
    uint8_t plen;
    uint16_t opcode;
    uint8_t sender_mac[6];
    uint32_t sender_ip;
    uint8_t target_mac[6];
    uint32_t target_ip;
} __attribute__((packed)) arp_packet_t;

typedef struct {
    uint8_t version_ihl;
    uint8_t dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ipv4_header_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t sequence;
    uint32_t acknowledgment;
    uint8_t data_offset_reserved;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

typedef struct {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
} __attribute__((packed)) icmp_echo_header_t;

typedef struct {
    uint16_t transaction_id;
    uint16_t flags;
    uint16_t questions;
    uint16_t answers;
    uint16_t authorities;
    uint16_t additionals;
} __attribute__((packed)) dns_header_t;

typedef struct {
    uint32_t ip;
    uint8_t mac[6];
    uint32_t last_seen_tick;
    int valid;
} arp_entry_t;

typedef struct {
    int present;
    uint8_t pci_bus;
    uint8_t pci_slot;
    uint8_t pci_func;
    uint32_t io_base;
    uint8_t mac[6];
    uint8_t tx_index;
    uint16_t rx_offset;
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t dhcp_server;
    uint16_t next_ip_id;
    uint16_t next_ping_seq;
    int configured;
    int used_qemu_fallback;
} net_state_t;

typedef struct {
    uint32_t yiaddr;
    uint32_t subnet;
    uint32_t gateway;
    uint32_t dns;
    uint32_t server_id;
    int valid;
} dhcp_offer_t;

typedef struct {
    int used;
    int type;
    net_socket_state_t state;
    int last_error;
    int peer_closed;
    int overflowed;
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t send_next;
    uint32_t recv_next;
    uint8_t rx_buf[NET_SOCKET_RX_BUFFER];
    uint16_t rx_len;
    uint8_t tx_buf[NET_SOCKET_TX_BUFFER];
    uint16_t tx_payload_len;
    uint32_t tx_sequence;
    uint8_t tx_flags;
    uint8_t tx_active;
    uint8_t tx_attempts;
    uint32_t tx_last_tick;
    uint32_t last_event_tick;
} net_socket_t;

static net_state_t net_state;
static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint8_t rtl_rx_buffer[RTL_RX_ALLOC_SIZE] __attribute__((aligned(256)));
static uint8_t rtl_tx_buffers[RTL_TX_BUFFER_COUNT][RTL_TX_BUFFER_SIZE] __attribute__((aligned(16)));

static uint32_t net_rng_state = 0x51F15EEDu;
static uint32_t dhcp_xid = 0;
static dhcp_offer_t dhcp_offer_state;
static dhcp_offer_t dhcp_ack_state;
static uint16_t pending_dns_id = 0;
static uint16_t pending_dns_port = 0;
static int pending_dns_status = -1;
static uint32_t pending_dns_result_ip = 0;
static uint16_t pending_ping_identifier = 0;
static uint16_t pending_ping_sequence = 0;
static uint32_t pending_ping_ip = 0;
static uint32_t pending_ping_started = 0;
static int pending_ping_status = -1;
static uint32_t pending_ping_rtt_ms = 0;
static uint16_t pending_udp_local_port = 0;
static uint16_t pending_udp_remote_port = 0;
static uint32_t pending_udp_remote_ip = 0;
static int pending_udp_status = -1;
static uint8_t* pending_udp_response_buf = 0;
static uint16_t pending_udp_response_buf_len = 0;
static uint16_t pending_udp_response_len = 0;
static uint32_t pending_udp_response_ip = 0;
static uint16_t pending_udp_response_port = 0;
static net_socket_t net_sockets[NET_SOCKET_COUNT];
static uint32_t net_last_ui_tick = 0;

static uint16_t net_swap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

static uint32_t net_swap32(uint32_t v) {
    return ((v & 0x000000FFU) << 24) |
           ((v & 0x0000FF00U) << 8) |
           ((v & 0x00FF0000U) >> 8) |
           ((v & 0xFF000000U) >> 24);
}

static uint16_t net_htons(uint16_t v) { return net_swap16(v); }
static uint16_t net_ntohs(uint16_t v) { return net_swap16(v); }
static uint32_t net_htonl(uint32_t v) { return net_swap32(v); }
static uint32_t net_ntohl(uint32_t v) { return net_swap32(v); }

static uint32_t net_random() {
    net_rng_state ^= timer_ticks + 0x9E3779B9u;
    net_rng_state ^= net_rng_state << 13;
    net_rng_state ^= net_rng_state >> 17;
    net_rng_state ^= net_rng_state << 5;
    if (net_rng_state == 0) net_rng_state = 0x6D2B79F5u;
    return net_rng_state;
}

static uint16_t net_random16() {
    return (uint16_t)(net_random() & 0xFFFFU);
}

static void net_pump_ui() {
    if (timer_ticks == net_last_ui_tick) return;
    net_last_ui_tick = timer_ticks;
    vbe_compose_scene_basic();
    vbe_update();
}

static void net_write16_be(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)((value >> 8) & 0xFF);
    dst[1] = (uint8_t)(value & 0xFF);
}

static void net_write32_be(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)((value >> 24) & 0xFF);
    dst[1] = (uint8_t)((value >> 16) & 0xFF);
    dst[2] = (uint8_t)((value >> 8) & 0xFF);
    dst[3] = (uint8_t)(value & 0xFF);
}

static uint32_t net_read32_be(const uint8_t* src) {
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
    if (length != 0) sum += (uint32_t)bytes[0] << 8;
    return sum;
}

static uint16_t net_checksum_finish(uint32_t sum) {
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
}

static uint16_t net_checksum16(const void* data, uint32_t length) {
    return net_checksum_finish(net_checksum_partial(0, data, length));
}

static uint16_t net_transport_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
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

static void net_print_hex_byte(uint8_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = digits[(value >> 4) & 0x0F];
    buf[1] = digits[value & 0x0F];
    buf[2] = '\0';
    vga_print(buf);
}

static void net_print_ip(uint32_t ip) {
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

static void net_print_unix_utc(uint32_t unix_seconds) {
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

static int net_append_text(char* dst, uint16_t dst_len, uint16_t* io_offset, const char* src) {
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

static int net_seq_lt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static int net_seq_gt(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

static uint32_t net_tcp_sequence_advance(uint8_t flags, uint16_t payload_len) {
    uint32_t advance = payload_len;

    if ((flags & TCP_FLAG_SYN) != 0) advance++;
    if ((flags & TCP_FLAG_FIN) != 0) advance++;
    return advance;
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

static void net_print_mac(const uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        net_print_hex_byte(mac[i]);
        if (i != 5) vga_putchar(':');
    }
}

static int net_parse_ipv4_text(const char* text, uint32_t* out_ip) {
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

static uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000U |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (uint32_t)(offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000U |
                       ((uint32_t)bus << 16) |
                       ((uint32_t)slot << 11) |
                       ((uint32_t)func << 8) |
                       (uint32_t)(offset & 0xFCU);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int pci_find_rtl8139(uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_config_read32((uint8_t)bus, slot, func, 0x00);
                uint16_t vendor = (uint16_t)(vendor_device & 0xFFFFU);
                uint16_t device = (uint16_t)((vendor_device >> 16) & 0xFFFFU);
                if (vendor == 0xFFFFU) {
                    if (func == 0) break;
                    continue;
                }
                if (vendor == RTL8139_VENDOR_ID && device == RTL8139_DEVICE_ID) {
                    *out_bus = (uint8_t)bus;
                    *out_slot = slot;
                    *out_func = func;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static int rtl8139_wait_reset() {
    uint32_t deadline = timer_ticks + NET_TIMEOUT_SHORT;
    while (timer_ticks <= deadline) {
        if ((inb((uint16_t)(net_state.io_base + RTL_REG_CR)) & RTL_CR_RESET) == 0) return 0;
    }
    return -1;
}

static int rtl8139_init_device() {
    uint8_t bus = 0, slot = 0, func = 0;
    uint32_t bar0;
    uint32_t command;

    memset(&net_state, 0, sizeof(net_state));
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(net_sockets, 0, sizeof(net_sockets));

    if (pci_find_rtl8139(&bus, &slot, &func) != 0) return -1;

    bar0 = pci_config_read32(bus, slot, func, 0x10);
    if ((bar0 & 0x1U) == 0) return -1;

    command = pci_config_read32(bus, slot, func, 0x04);
    command |= 0x00000005U;
    pci_config_write32(bus, slot, func, 0x04, command);

    net_state.present = 1;
    net_state.pci_bus = bus;
    net_state.pci_slot = slot;
    net_state.pci_func = func;
    net_state.io_base = bar0 & ~0x3U;
    net_state.next_ip_id = 1;
    net_state.next_ping_seq = 1;

    outb((uint16_t)(net_state.io_base + RTL_REG_CONFIG1), 0x00);
    outb((uint16_t)(net_state.io_base + RTL_REG_CR), RTL_CR_RESET);
    if (rtl8139_wait_reset() != 0) return -1;

    outl((uint16_t)(net_state.io_base + RTL_REG_RBSTART), (uint32_t)rtl_rx_buffer);
    for (uint8_t i = 0; i < RTL_TX_BUFFER_COUNT; i++) {
        outl((uint16_t)(net_state.io_base + RTL_REG_TSAD0 + i * 4U), (uint32_t)rtl_tx_buffers[i]);
    }

    outw((uint16_t)(net_state.io_base + RTL_REG_IMR), 0x0000);
    outw((uint16_t)(net_state.io_base + RTL_REG_ISR), 0xFFFF);
    outl((uint16_t)(net_state.io_base + RTL_REG_TCR), 0x03000000U);
    outl((uint16_t)(net_state.io_base + RTL_REG_RCR), 0x0000008FU);
    outw((uint16_t)(net_state.io_base + RTL_REG_CAPR), 0xFFF0U);
    outb((uint16_t)(net_state.io_base + RTL_REG_CR), RTL_CR_RX_ENABLE | RTL_CR_TX_ENABLE);

    for (int i = 0; i < 6; i++) {
        net_state.mac[i] = inb((uint16_t)(net_state.io_base + RTL_REG_IDR0 + i));
    }

    return 0;
}

static int rtl8139_send_frame(const void* frame, uint32_t length) {
    uint8_t index;
    uint32_t tx_length;
    uint32_t status_reg;
    uint32_t deadline;

    if (!net_state.present || !frame || length == 0 || length > RTL_TX_BUFFER_SIZE) return -1;

    index = net_state.tx_index & 0x03U;
    tx_length = length < 60U ? 60U : length;
    memset(rtl_tx_buffers[index], 0, RTL_TX_BUFFER_SIZE);
    memcpy(rtl_tx_buffers[index], frame, length);

    outl((uint16_t)(net_state.io_base + RTL_REG_TSAD0 + index * 4U), (uint32_t)rtl_tx_buffers[index]);
    outl((uint16_t)(net_state.io_base + RTL_REG_TSD0 + index * 4U), tx_length);

    deadline = timer_ticks + NET_TIMEOUT_SHORT;
    status_reg = net_state.io_base + RTL_REG_TSD0 + index * 4U;
    while (timer_ticks <= deadline) {
        uint32_t status = inl((uint16_t)status_reg);
        if ((status & RTL_TSD_TOK) != 0) {
            net_state.tx_index = (uint8_t)((index + 1U) & 0x03U);
            return 0;
        }
        if ((status & (RTL_TSD_TABT | RTL_TSD_TUN)) != 0) return -1;
    }
    return -1;
}

static void arp_cache_update(uint32_t ip, const uint8_t* mac) {
    int slot = -1;
    uint32_t oldest_tick = 0xFFFFFFFFU;
    int oldest_idx = 0;

    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].last_seen_tick = timer_ticks;
            return;
        }
        if (!arp_cache[i].valid && slot == -1) slot = i;
        if (arp_cache[i].last_seen_tick < oldest_tick) {
            oldest_tick = arp_cache[i].last_seen_tick;
            oldest_idx = i;
        }
    }

    if (slot == -1) slot = oldest_idx;
    arp_cache[slot].valid = 1;
    arp_cache[slot].ip = ip;
    memcpy(arp_cache[slot].mac, mac, 6);
    arp_cache[slot].last_seen_tick = timer_ticks;
}

static int arp_cache_lookup(uint32_t ip, uint8_t* out_mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(out_mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static int net_route_ip(uint32_t dest_ip, uint32_t* out_next_hop) {
    if (!out_next_hop) return -1;
    if (dest_ip == 0xFFFFFFFFU) {
        *out_next_hop = dest_ip;
        return 0;
    }
    if (!net_state.configured) return -1;
    if ((dest_ip & net_state.netmask) == (net_state.ip_addr & net_state.netmask)) {
        *out_next_hop = dest_ip;
    } else {
        *out_next_hop = net_state.gateway;
    }
    return 0;
}

static int net_send_ethernet(const uint8_t* dst_mac, uint16_t ether_type, const void* payload, uint16_t payload_len) {
    uint8_t frame[1514];
    eth_header_t* eth = (eth_header_t*)frame;
    uint32_t frame_len = sizeof(eth_header_t) + payload_len;

    if (frame_len > sizeof(frame)) return -1;

    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, net_state.mac, 6);
    eth->type = net_htons(ether_type);
    memcpy(frame + sizeof(eth_header_t), payload, payload_len);

    return rtl8139_send_frame(frame, frame_len);
}

static int net_send_arp_packet(uint16_t opcode, const uint8_t* target_mac, uint32_t sender_ip, uint32_t target_ip) {
    uint8_t frame[sizeof(arp_packet_t)];
    arp_packet_t* arp = (arp_packet_t*)frame;

    arp->htype = net_htons(1);
    arp->ptype = net_htons(ETH_TYPE_IPV4);
    arp->hlen = 6;
    arp->plen = 4;
    arp->opcode = net_htons(opcode);
    memcpy(arp->sender_mac, net_state.mac, 6);
    arp->sender_ip = net_htonl(sender_ip);
    if (target_mac) memcpy(arp->target_mac, target_mac, 6);
    else memset(arp->target_mac, 0, 6);
    arp->target_ip = net_htonl(target_ip);

    if (opcode == 1) {
        static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        return net_send_ethernet(broadcast_mac, ETH_TYPE_ARP, frame, sizeof(frame));
    }
    return net_send_ethernet(target_mac, ETH_TYPE_ARP, frame, sizeof(frame));
}

static void net_handle_frame(const uint8_t* frame, uint16_t frame_len);
int net_udp_exchange(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                     const void* payload, uint16_t payload_len,
                     void* response_buf, uint16_t response_buf_len,
                     net_udp_response_info_t* out_info, uint32_t timeout_ticks);

static void net_clear_pending_udp() {
    pending_udp_local_port = 0;
    pending_udp_remote_port = 0;
    pending_udp_remote_ip = 0;
    pending_udp_status = -1;
    pending_udp_response_buf = 0;
    pending_udp_response_buf_len = 0;
    pending_udp_response_len = 0;
    pending_udp_response_ip = 0;
    pending_udp_response_port = 0;
}

static void rtl8139_poll_receive() {
    while (net_state.present && ((inb((uint16_t)(net_state.io_base + RTL_REG_CR)) & RTL_CR_BUFE) == 0)) {
        uint8_t* packet = rtl_rx_buffer + net_state.rx_offset;
        uint16_t status = (uint16_t)(packet[0] | ((uint16_t)packet[1] << 8));
        uint16_t packet_len = (uint16_t)(packet[2] | ((uint16_t)packet[3] << 8));
        uint16_t payload_len;

        if ((status & 0x0001U) == 0 || packet_len < 4U || packet_len > 1600U) {
            net_state.rx_offset = 0;
            outw((uint16_t)(net_state.io_base + RTL_REG_CAPR), 0xFFF0U);
            outw((uint16_t)(net_state.io_base + RTL_REG_ISR), 0xFFFF);
            break;
        }

        payload_len = packet_len > 4U ? (uint16_t)(packet_len - 4U) : packet_len;
        net_handle_frame(packet + 4, payload_len);

        net_state.rx_offset = (uint16_t)((net_state.rx_offset + packet_len + 4U + 3U) & ~3U);
        net_state.rx_offset %= (uint16_t)RTL_RX_BUFFER_SIZE;
        outw((uint16_t)(net_state.io_base + RTL_REG_CAPR), (uint16_t)(net_state.rx_offset - 16U));
        outw((uint16_t)(net_state.io_base + RTL_REG_ISR), 0xFFFF);
    }
}

static int net_wait_until(int* status_ptr, uint32_t timeout_ticks) {
    uint32_t deadline = timer_ticks + timeout_ticks;
    while (timer_ticks <= deadline) {
        rtl8139_poll_receive();
        net_pump_ui();
        if (*status_ptr != -1) return 0;
        asm volatile("hlt");
    }
    return -1;
}

static int net_ensure_configured() {
    if (!net_state.present) return -1;
    if (!net_state.configured) {
        (void)net_run_dhcp(0);
    }
    return net_state.configured ? 0 : -1;
}

static int net_resolve_mac(uint32_t target_ip, uint8_t* out_mac) {
    static const uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t routed_ip;
    uint32_t deadline;

    if (target_ip == 0xFFFFFFFFU) {
        memcpy(out_mac, broadcast_mac, 6);
        return 0;
    }
    if (net_route_ip(target_ip, &routed_ip) != 0) return -1;
    if (arp_cache_lookup(routed_ip, out_mac) == 0) return 0;
    if (net_send_arp_packet(1, 0, net_state.ip_addr, routed_ip) != 0) return -1;

    deadline = timer_ticks + NET_TIMEOUT_SHORT;
    while (timer_ticks <= deadline) {
        rtl8139_poll_receive();
        net_pump_ui();
        if (arp_cache_lookup(routed_ip, out_mac) == 0) return 0;
        asm volatile("hlt");
    }
    return -1;
}

static int net_send_ipv4_packet(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol, const void* payload, uint16_t payload_len, const uint8_t* forced_dst_mac) {
    uint8_t packet[1514];
    uint8_t dst_mac[6];
    eth_header_t* eth = (eth_header_t*)packet;
    ipv4_header_t* ip = (ipv4_header_t*)(packet + sizeof(eth_header_t));
    uint8_t* ip_payload = packet + sizeof(eth_header_t) + sizeof(ipv4_header_t);
    uint16_t total_length = (uint16_t)(sizeof(ipv4_header_t) + payload_len);

    if (sizeof(eth_header_t) + total_length > sizeof(packet)) return -1;

    if (forced_dst_mac) memcpy(dst_mac, forced_dst_mac, 6);
    else if (net_resolve_mac(dst_ip, dst_mac) != 0) return -1;

    memcpy(eth->dst, dst_mac, 6);
    memcpy(eth->src, net_state.mac, 6);
    eth->type = net_htons(ETH_TYPE_IPV4);

    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->total_length = net_htons(total_length);
    ip->identification = net_htons(net_state.next_ip_id++);
    ip->flags_fragment = net_htons(0x4000);
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->src_ip = net_htonl(src_ip);
    ip->dst_ip = net_htonl(dst_ip);
    ip->checksum = 0;
    ip->checksum = net_htons(net_checksum16(ip, sizeof(*ip)));

    memcpy(ip_payload, payload, payload_len);
    return rtl8139_send_frame(packet, (uint32_t)(sizeof(eth_header_t) + total_length));
}

static int net_send_udp(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, const void* payload, uint16_t payload_len, const uint8_t* forced_dst_mac) {
    uint8_t udp_packet[1472];
    udp_header_t* udp = (udp_header_t*)udp_packet;
    uint8_t* udp_payload = udp_packet + sizeof(udp_header_t);
    uint16_t total_len = (uint16_t)(sizeof(udp_header_t) + payload_len);

    if (total_len > sizeof(udp_packet)) return -1;

    udp->src_port = net_htons(src_port);
    udp->dst_port = net_htons(dst_port);
    udp->length = net_htons(total_len);
    udp->checksum = 0;
    memcpy(udp_payload, payload, payload_len);

    return net_send_ipv4_packet(src_ip, dst_ip, IPV4_PROTOCOL_UDP, udp_packet, total_len, forced_dst_mac);
}

static int net_send_icmp_echo(uint32_t dst_ip, uint16_t identifier, uint16_t sequence) {
    uint8_t packet[64];
    icmp_echo_header_t* icmp = (icmp_echo_header_t*)packet;
    uint8_t* payload = packet + sizeof(icmp_echo_header_t);
    uint16_t payload_len = 24;
    uint16_t packet_len = (uint16_t)(sizeof(icmp_echo_header_t) + payload_len);

    memset(packet, 0, sizeof(packet));
    icmp->type = 8;
    icmp->code = 0;
    icmp->identifier = net_htons(identifier);
    icmp->sequence = net_htons(sequence);
    for (uint16_t i = 0; i < payload_len; i++) {
        payload[i] = (uint8_t)('A' + (i % 26));
    }
    icmp->checksum = 0;
    icmp->checksum = net_htons(net_checksum16(packet, packet_len));

    return net_send_ipv4_packet(net_state.ip_addr, dst_ip, IPV4_PROTOCOL_ICMP, packet, packet_len, 0);
}

static int net_send_tcp_segment(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                                uint32_t sequence, uint32_t acknowledgment,
                                uint8_t flags, uint16_t window,
                                const void* payload, uint16_t payload_len) {
    uint8_t packet[1480];
    tcp_header_t* tcp = (tcp_header_t*)packet;
    uint16_t total_len = (uint16_t)(sizeof(tcp_header_t) + payload_len);

    if ((payload_len != 0U && !payload) || total_len > sizeof(packet)) return -1;

    memset(packet, 0, sizeof(packet));
    tcp->src_port = net_htons(src_port);
    tcp->dst_port = net_htons(dst_port);
    tcp->sequence = net_htonl(sequence);
    tcp->acknowledgment = net_htonl(acknowledgment);
    tcp->data_offset_reserved = 0x50;
    tcp->flags = flags;
    tcp->window = net_htons(window);
    tcp->urgent_pointer = 0;
    if (payload_len != 0U) {
        memcpy(packet + sizeof(tcp_header_t), payload, payload_len);
    }
    tcp->checksum = 0;
    tcp->checksum = net_htons(net_transport_checksum(net_state.ip_addr, dst_ip, IPV4_PROTOCOL_TCP,
                                                     packet, total_len));

    return net_send_ipv4_packet(net_state.ip_addr, dst_ip, IPV4_PROTOCOL_TCP, packet, total_len, 0);
}

static void net_socket_reset(net_socket_t* socket) {
    if (!socket) return;
    memset(socket, 0, sizeof(*socket));
    socket->state = NET_SOCKET_STATE_IDLE;
}

static net_socket_t* net_socket_from_handle(int handle) {
    if (handle < 1 || handle > NET_SOCKET_COUNT) return 0;
    if (!net_sockets[handle - 1].used) return 0;
    return &net_sockets[handle - 1];
}

static net_socket_t* net_socket_find_tcp(uint32_t src_ip, uint16_t src_port, uint16_t dst_port) {
    for (int i = 0; i < NET_SOCKET_COUNT; i++) {
        net_socket_t* socket = &net_sockets[i];
        if (!socket->used || socket->type != NET_SOCK_STREAM) continue;
        if (socket->remote_ip == src_ip &&
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

static uint16_t net_socket_alloc_local_port() {
    uint16_t base = (uint16_t)(41000U + (net_random16() % 2000U));

    for (uint16_t i = 0; i < 2000U; i++) {
        uint16_t port = (uint16_t)(41000U + ((base - 41000U + i) % 2000U));
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
    uint32_t acknowledgment = ((flags & TCP_FLAG_SYN) != 0) ? 0U : socket->recv_next;

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

    local_port = net_socket_alloc_local_port();
    if (local_port == 0U) return NET_ERR_NO_SOCKETS;

    socket->state = NET_SOCKET_STATE_CONNECTING;
    socket->last_error = 0;
    socket->peer_closed = 0;
    socket->overflowed = 0;
    socket->remote_ip = remote_ip;
    socket->local_port = local_port;
    socket->remote_port = remote_port;
    socket->send_next = 0x40000000U ^ net_random();
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
        net_append_text(request, sizeof(request), &request_off, "\r\nUser-Agent: NarcOs/0.1\r\nConnection: close\r\n\r\n") != 0) {
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
        if (opt == key && len >= 4) {
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
        if (opt == key && len >= 1) {
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

    if (requested_ip != 0) {
        options[options_len++] = 50;
        options[options_len++] = 4;
        net_write32_be(options + options_len, requested_ip);
        options_len += 4;
    }
    if (server_id != 0) {
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
        if (len == 0) {
            off++;
            if (!jumped) end_offset = off;
            *out_offset = end_offset;
            return 0;
        }
        if ((len & 0xC0) == 0xC0) {
            if (off + 1 >= packet_len) return -1;
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

static int net_dns_lookup(const char* host, uint32_t* out_ip) {
    uint8_t packet[512];
    dns_header_t* dns = (dns_header_t*)packet;
    uint16_t offset = sizeof(dns_header_t);
    const char* cursor = host;

    if (!net_state.configured || net_state.dns_server == 0) return -1;

    memset(packet, 0, sizeof(packet));
    pending_dns_id = net_random16();
    pending_dns_port = (uint16_t)(40000U + (net_random16() % 2000U));
    pending_dns_status = -1;
    pending_dns_result_ip = 0;

    dns->transaction_id = net_htons(pending_dns_id);
    dns->flags = net_htons(0x0100);
    dns->questions = net_htons(1);

    while (*cursor && offset < sizeof(packet) - 6) {
        uint16_t label_start = offset++;
        uint8_t label_len = 0;
        while (*cursor && *cursor != '.') {
            packet[offset++] = (uint8_t)*cursor++;
            label_len++;
            if (offset >= sizeof(packet) - 6 || label_len > 63) return -1;
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
            server_id = dhcp_offer_state.server_id != 0 ? dhcp_offer_state.server_id : dhcp_offer_state.gateway;
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
                    net_state.netmask = dhcp_ack_state.subnet != 0 ? dhcp_ack_state.subnet : 0xFFFFFF00U;
                    net_state.gateway = dhcp_ack_state.gateway != 0 ? dhcp_ack_state.gateway : server_id;
                    net_state.dns_server = dhcp_ack_state.dns != 0 ? dhcp_ack_state.dns : net_state.gateway;
                    net_state.dhcp_server = dhcp_ack_state.server_id != 0 ? dhcp_ack_state.server_id : server_id;
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

static void net_handle_arp(const uint8_t* frame, uint16_t frame_len) {
    const arp_packet_t* arp;
    uint16_t opcode;
    uint32_t sender_ip;
    uint32_t target_ip;

    if (frame_len < sizeof(arp_packet_t)) return;
    arp = (const arp_packet_t*)frame;
    opcode = net_ntohs(arp->opcode);
    sender_ip = net_ntohl(arp->sender_ip);
    target_ip = net_ntohl(arp->target_ip);

    arp_cache_update(sender_ip, arp->sender_mac);

    if (opcode == 1 && net_state.configured && target_ip == net_state.ip_addr) {
        net_send_arp_packet(2, arp->sender_mac, net_state.ip_addr, sender_ip);
    }
}

static void net_handle_dns_payload(const uint8_t* payload, uint16_t payload_len) {
    const dns_header_t* dns;
    uint16_t answers;
    uint16_t offset;

    if (payload_len < sizeof(dns_header_t)) return;
    dns = (const dns_header_t*)payload;
    if (net_ntohs(dns->transaction_id) != pending_dns_id) return;
    if ((net_ntohs(dns->flags) & 0x8000U) == 0) return;

    answers = net_ntohs(dns->answers);
    offset = sizeof(dns_header_t);

    for (uint16_t i = 0; i < net_ntohs(dns->questions); i++) {
        if (dns_skip_name(payload, payload_len, offset, &offset) != 0) {
            pending_dns_status = 1;
            return;
        }
        if (offset + 4 > payload_len) {
            pending_dns_status = 1;
            return;
        }
        offset = (uint16_t)(offset + 4);
    }

    for (uint16_t i = 0; i < answers; i++) {
        uint16_t answer_type;
        uint16_t answer_class;
        uint16_t rdlength;

        if (dns_skip_name(payload, payload_len, offset, &offset) != 0) break;
        if (offset + 10 > payload_len) break;

        answer_type = (uint16_t)(((uint16_t)payload[offset] << 8) | payload[offset + 1]);
        answer_class = (uint16_t)(((uint16_t)payload[offset + 2] << 8) | payload[offset + 3]);
        rdlength = (uint16_t)(((uint16_t)payload[offset + 8] << 8) | payload[offset + 9]);
        offset = (uint16_t)(offset + 10);

        if (offset + rdlength > payload_len) break;
        if (answer_type == 1 && answer_class == 1 && rdlength == 4) {
            pending_dns_result_ip = net_read32_be(payload + offset);
            pending_dns_status = 0;
            return;
        }
        offset = (uint16_t)(offset + rdlength);
    }

    pending_dns_status = 1;
}

static void net_handle_dhcp_payload(const uint8_t* payload, uint16_t payload_len) {
    const uint8_t* options;
    uint16_t options_len;
    uint8_t msg_type = 0;
    uint32_t packet_xid;
    uint32_t yiaddr;
    dhcp_offer_t parsed;

    if (payload_len < 240) return;
    packet_xid = net_read32_be(payload + 4);
    if (packet_xid != dhcp_xid) return;
    if (net_read32_be(payload + 236) != DHCP_MAGIC_COOKIE) return;

    yiaddr = net_read32_be(payload + 16);
    options = payload + 240;
    options_len = (uint16_t)(payload_len - 240);
    if (dhcp_option_u8(options, options_len, 53, &msg_type) != 0) return;

    memset(&parsed, 0, sizeof(parsed));
    parsed.yiaddr = yiaddr;
    parsed.valid = 1;
    dhcp_option_u32(options, options_len, 1, &parsed.subnet);
    dhcp_option_u32(options, options_len, 3, &parsed.gateway);
    dhcp_option_u32(options, options_len, 6, &parsed.dns);
    dhcp_option_u32(options, options_len, 54, &parsed.server_id);

    if (msg_type == 2) dhcp_offer_state = parsed;
    else if (msg_type == 5) dhcp_ack_state = parsed;
}

static void net_handle_tcp_payload(uint32_t src_ip, uint32_t dst_ip, const uint8_t* payload, uint16_t payload_len) {
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

    if ((flags & TCP_FLAG_RST) != 0) {
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

    if ((flags & TCP_FLAG_ACK) != 0) {
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

    if ((tcp_payload_len != 0U || (flags & TCP_FLAG_FIN) != 0) && sequence != socket->recv_next) {
        if ((flags & (TCP_FLAG_FIN | TCP_FLAG_SYN)) != 0 || tcp_payload_len != 0U) {
            (void)net_socket_transmit(socket, socket->send_next, TCP_FLAG_ACK, 0, 0);
        }
        return;
    }

    if (tcp_payload_len != 0U) {
        if (net_socket_append_rx(socket, tcp_payload, tcp_payload_len) != NET_OK) return;
        socket->recv_next += tcp_payload_len;
    }

    if ((flags & TCP_FLAG_FIN) != 0) {
        socket->recv_next += 1U;
        socket->peer_closed = 1;
        if (socket->state == NET_SOCKET_STATE_ESTABLISHED) {
            socket->state = NET_SOCKET_STATE_CLOSE_WAIT;
        }
    }

    if (tcp_payload_len != 0U || (flags & TCP_FLAG_FIN) != 0) {
        if (net_socket_transmit(socket, socket->send_next, TCP_FLAG_ACK, 0, 0) != NET_OK) return;
    }

    if (socket->state == NET_SOCKET_STATE_CLOSING && socket->peer_closed && !socket->tx_active) {
        socket->state = NET_SOCKET_STATE_CLOSED;
    }
}

static void net_handle_icmp_payload(uint32_t src_ip, const uint8_t* payload, uint16_t payload_len) {
    const icmp_echo_header_t* icmp;
    uint16_t identifier;
    uint16_t sequence;

    if (payload_len < sizeof(icmp_echo_header_t)) return;
    icmp = (const icmp_echo_header_t*)payload;
    if (icmp->type != 0 || icmp->code != 0) return;

    identifier = net_ntohs(icmp->identifier);
    sequence = net_ntohs(icmp->sequence);
    if (identifier == pending_ping_identifier && sequence == pending_ping_sequence && src_ip == pending_ping_ip) {
        pending_ping_rtt_ms = (timer_ticks - pending_ping_started) * 10U;
        pending_ping_status = 0;
    }
}

static void net_handle_ipv4(const uint8_t* eth_src, const uint8_t* frame, uint16_t frame_len) {
    const ipv4_header_t* ip;
    uint16_t ihl;
    uint16_t total_length;
    uint32_t src_ip;
    uint32_t dst_ip;
    const uint8_t* payload;
    uint16_t payload_len;

    if (frame_len < sizeof(ipv4_header_t)) return;
    ip = (const ipv4_header_t*)frame;
    if ((ip->version_ihl >> 4) != 4) return;

    ihl = (uint16_t)((ip->version_ihl & 0x0F) * 4U);
    if (ihl < sizeof(ipv4_header_t) || frame_len < ihl) return;

    total_length = net_ntohs(ip->total_length);
    if (total_length < ihl) return;
    if (total_length > frame_len) total_length = frame_len;

    src_ip = net_ntohl(ip->src_ip);
    dst_ip = net_ntohl(ip->dst_ip);
    arp_cache_update(src_ip, eth_src);

    payload = frame + ihl;
    payload_len = (uint16_t)(total_length - ihl);

    if (ip->protocol == IPV4_PROTOCOL_ICMP) {
        if (net_state.configured && dst_ip == net_state.ip_addr) {
            net_handle_icmp_payload(src_ip, payload, payload_len);
        }
        return;
    }

    if (ip->protocol == IPV4_PROTOCOL_TCP) {
        net_handle_tcp_payload(src_ip, dst_ip, payload, payload_len);
        return;
    }

    if (ip->protocol == IPV4_PROTOCOL_UDP) {
        const udp_header_t* udp;
        uint16_t src_port;
        uint16_t dst_port;
        const uint8_t* udp_payload;
        uint16_t udp_payload_len;

        if (payload_len < sizeof(udp_header_t)) return;
        udp = (const udp_header_t*)payload;
        src_port = net_ntohs(udp->src_port);
        dst_port = net_ntohs(udp->dst_port);
        udp_payload = payload + sizeof(udp_header_t);
        udp_payload_len = payload_len > sizeof(udp_header_t) ? (uint16_t)(payload_len - sizeof(udp_header_t)) : 0;

        if (dst_port == DHCP_CLIENT_PORT && src_port == DHCP_SERVER_PORT) {
            net_handle_dhcp_payload(udp_payload, udp_payload_len);
        } else if (dst_port == pending_dns_port && src_port == DNS_SERVER_PORT &&
                   net_state.configured && dst_ip == net_state.ip_addr) {
            net_handle_dns_payload(udp_payload, udp_payload_len);
        } else if (pending_udp_status == -1 &&
                   pending_udp_local_port != 0 &&
                   dst_port == pending_udp_local_port &&
                   (pending_udp_remote_port == 0 || src_port == pending_udp_remote_port) &&
                   (pending_udp_remote_ip == 0 || src_ip == pending_udp_remote_ip) &&
                   net_state.configured && dst_ip == net_state.ip_addr) {
            uint16_t copy_len = udp_payload_len;
            if (copy_len > pending_udp_response_buf_len) copy_len = pending_udp_response_buf_len;
            if (copy_len != 0U) memcpy(pending_udp_response_buf, udp_payload, copy_len);
            pending_udp_response_len = copy_len;
            pending_udp_response_ip = src_ip;
            pending_udp_response_port = src_port;
            pending_udp_status = 0;
        }
    }
}

static void net_handle_frame(const uint8_t* frame, uint16_t frame_len) {
    const eth_header_t* eth;
    uint16_t ether_type;

    if (frame_len < sizeof(eth_header_t)) return;
    eth = (const eth_header_t*)frame;
    ether_type = net_ntohs(eth->type);

    if (ether_type == ETH_TYPE_ARP) {
        net_handle_arp(frame + sizeof(eth_header_t), (uint16_t)(frame_len - sizeof(eth_header_t)));
    } else if (ether_type == ETH_TYPE_IPV4) {
        net_handle_ipv4(eth->src, frame + sizeof(eth_header_t), (uint16_t)(frame_len - sizeof(eth_header_t)));
    }
}

void net_init() {
    if (rtl8139_init_device() == 0) {
        (void)net_run_dhcp(0);
    }
}

void net_poll() {
    rtl8139_poll_receive();
}

int net_is_available() {
    return net_state.present;
}

int net_is_configured() {
    return net_state.present && net_state.configured;
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

int net_udp_exchange(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                     const void* payload, uint16_t payload_len,
                     void* response_buf, uint16_t response_buf_len,
                     net_udp_response_info_t* out_info, uint32_t timeout_ticks) {
    uint16_t local_port = src_port;

    if ((payload_len != 0U && !payload) || !response_buf || response_buf_len == 0U || !out_info) return -1;
    if (net_ensure_configured() != 0) return -1;

    if (local_port == 0U) {
        local_port = (uint16_t)(40000U + (net_random16() % 2000U));
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

static int net_parse_http_target(const char* target, char* host, uint16_t host_len, char* path, uint16_t path_len) {
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

int net_ping_command(const char* target) {
    uint32_t ip;
    uint16_t identifier;

    if (!target || target[0] == '\0') {
        vga_print_color("Usage: ping <host>\n", 0x0E);
        return -1;
    }
    if (!net_state.present) {
        vga_print_color("error: RTL8139 NIC not found. Start QEMU with rtl8139 enabled.\n", 0x0C);
        return -1;
    }
    if (net_resolve_ipv4(target, &ip) != 0) {
        vga_print_color("error: Failed to resolve target host.\n", 0x0C);
        return -1;
    }

    vga_print("Pinging ");
    vga_print(target);
    vga_print(" [");
    net_print_ip(ip);
    vga_println("] ...");

    identifier = (uint16_t)(0xB000U | (net_random16() & 0x0FFFU));
    for (int i = 0; i < 4; i++) {
        pending_ping_identifier = identifier;
        pending_ping_sequence = net_state.next_ping_seq++;
        pending_ping_ip = ip;
        pending_ping_started = timer_ticks;
        pending_ping_status = -1;
        pending_ping_rtt_ms = 0;

        if (net_send_icmp_echo(ip, pending_ping_identifier, pending_ping_sequence) != 0) {
            vga_print_color("error: Failed to transmit ICMP packet.\n", 0x0C);
            return -1;
        }
        if (net_wait_until(&pending_ping_status, NET_TIMEOUT_LONG) == 0 && pending_ping_status == 0) {
            vga_print("Reply from ");
            net_print_ip(ip);
            vga_print(": time=");
            vga_print_int((int)pending_ping_rtt_ms);
            vga_println("ms");
        } else {
            vga_println("Request timed out.");
            pending_ping_status = -1;
        }
    }
    return 0;
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
