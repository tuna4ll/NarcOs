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

static uint16_t net_checksum16(const void* data, uint32_t length) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint32_t sum = 0;

    while (length > 1) {
        sum += ((uint32_t)bytes[0] << 8) | (uint32_t)bytes[1];
        bytes += 2;
        length -= 2;
    }
    if (length != 0) sum += (uint32_t)bytes[0] << 8;

    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
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

int net_dns_command(const char* host) {
    uint32_t ip;

    if (!host || host[0] == '\0') {
        vga_print_color("Usage: dns <host>\n", 0x0E);
        return -1;
    }
    if (!net_state.present) {
        vga_print_color("error: Network driver is not ready.\n", 0x0C);
        return -1;
    }
    if (!net_state.configured && net_run_dhcp(0) < 0) {
        vga_print_color("error: Failed to obtain IPv4 configuration.\n", 0x0C);
        return -1;
    }
    if (net_parse_ipv4_text(host, &ip) == 0) {
        net_print_ip(ip);
        vga_println("");
        return 0;
    }
    if (net_dns_lookup(host, &ip) != 0) {
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
    if (!net_state.configured && net_run_dhcp(0) < 0) {
        vga_print_color("error: Failed to obtain IPv4 configuration.\n", 0x0C);
        return -1;
    }
    if (net_parse_ipv4_text(target, &ip) != 0 && net_dns_lookup(target, &ip) != 0) {
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
