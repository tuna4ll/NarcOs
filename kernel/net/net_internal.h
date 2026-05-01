#ifndef NET_INTERNAL_H
#define NET_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "net.h"
#include "cpu.h"
#include "io.h"
#include "pci.h"
#include "rtc.h"
#include "process.h"
#include "string.h"

extern void vga_print(const char* str);
extern void vga_println(const char* str);
extern void vga_print_int(int num);
extern void vga_print_color(const char* str, uint8_t color);
extern void vga_putchar(char c);
extern volatile uint32_t timer_ticks;
extern void vbe_compose_scene_basic();
extern volatile int gui_needs_redraw;

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
#define NET_EPHEMERAL_PORT_BASE_UDP 40000U
#define NET_EPHEMERAL_PORT_BASE_TCP 41000U
#define NET_EPHEMERAL_PORT_COUNT    2000U
#define NET_TCP_ISN_TICK_INCREMENT  2500U

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
    uint8_t irq_line;
    uint8_t irq_pin;
    uint8_t irq_enabled;
    uint32_t io_base;
    uint8_t mac[6];
    uint8_t tx_index;
    uint16_t rx_offset;
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns_server;
    uint32_t dhcp_server;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_packets;
    uint32_t tx_packets;
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

extern net_state_t net_state;
extern arp_entry_t arp_cache[ARP_CACHE_SIZE];
extern uint8_t rtl_rx_buffer[RTL_RX_ALLOC_SIZE];
extern uint8_t rtl_tx_buffers[RTL_TX_BUFFER_COUNT][RTL_TX_BUFFER_SIZE];
extern uint32_t net_secret[4];
extern uint32_t net_prng_counter;
extern uint32_t net_isn_last_tick;
extern uint16_t net_isn_tick_offset;
extern int net_secret_ready;
extern uint32_t dhcp_xid;
extern dhcp_offer_t dhcp_offer_state;
extern dhcp_offer_t dhcp_ack_state;
extern uint16_t pending_dns_id;
extern uint16_t pending_dns_port;
extern int pending_dns_status;
extern uint32_t pending_dns_result_ip;
extern uint16_t pending_ping_identifier;
extern uint16_t pending_ping_sequence;
extern uint32_t pending_ping_ip;
extern uint32_t pending_ping_started;
extern int pending_ping_status;
extern uint32_t pending_ping_rtt_ms;
extern uint16_t pending_udp_local_port;
extern uint16_t pending_udp_remote_port;
extern uint32_t pending_udp_remote_ip;
extern int pending_udp_status;
extern uint8_t* pending_udp_response_buf;
extern uint16_t pending_udp_response_buf_len;
extern uint16_t pending_udp_response_len;
extern uint32_t pending_udp_response_ip;
extern uint16_t pending_udp_response_port;
extern net_socket_t net_sockets[NET_SOCKET_COUNT];
extern uint32_t net_last_ui_tick;

uint16_t net_swap16(uint16_t v);
uint32_t net_swap32(uint32_t v);
uint16_t net_htons(uint16_t v);
uint16_t net_ntohs(uint16_t v);
uint32_t net_htonl(uint32_t v);
uint32_t net_ntohl(uint32_t v);
void net_pump_ui();
void net_write16_le(uint8_t* dst, uint16_t value);
void net_write32_le(uint8_t* dst, uint32_t value);
uint32_t net_read32_le(const uint8_t* src);
void net_write16_be(uint8_t* dst, uint16_t value);
void net_write32_be(uint8_t* dst, uint32_t value);
uint32_t net_read32_be(const uint8_t* src);
uint16_t net_checksum16(const void* data, uint32_t length);
uint16_t net_transport_checksum(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
                                const void* segment, uint16_t segment_len);
uint32_t net_random();
uint16_t net_random16();
uint32_t net_tcp_initial_sequence(uint32_t local_ip, uint16_t local_port,
                                  uint32_t remote_ip, uint16_t remote_port);
uint16_t net_pick_ephemeral_port(uint16_t base, uint16_t count,
                                 uint32_t remote_ip, uint16_t remote_port,
                                 uint32_t salt);
void net_print_hex_byte(uint8_t value);
void net_print_ip(uint32_t ip);
void net_print_mac(const uint8_t* mac);
void net_print_unix_utc(uint32_t unix_seconds);
int net_append_text(char* dst, uint16_t dst_len, uint16_t* io_offset, const char* src);
int net_parse_ipv4_text(const char* text, uint32_t* out_ip);

int rtl8139_init_device();
void rtl8139_poll_receive();
int net_wait_until(int* status_ptr, uint32_t timeout_ticks);
int net_ensure_configured();
int net_send_udp(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const void* payload, uint16_t payload_len, const uint8_t* forced_dst_mac);
int net_send_icmp_echo(uint32_t dst_ip, uint16_t identifier, uint16_t sequence);
int net_send_tcp_segment(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                         uint32_t sequence, uint32_t acknowledgment,
                         uint8_t flags, uint16_t window,
                         const void* payload, uint16_t payload_len);
void net_clear_pending_udp();

int net_dns_lookup(const char* host, uint32_t* out_ip);
void net_handle_dns_payload(const uint8_t* payload, uint16_t payload_len);
void net_handle_dhcp_payload(const uint8_t* payload, uint16_t payload_len);
void net_handle_icmp_payload(uint32_t src_ip, const uint8_t* payload, uint16_t payload_len);
void net_handle_tcp_payload(uint32_t src_ip, uint32_t dst_ip, const uint8_t* payload, uint16_t payload_len);

#endif
