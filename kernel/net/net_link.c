#include "net_internal.h"

static int rtl8139_wait_reset() {
    uint32_t deadline = timer_ticks + NET_TIMEOUT_SHORT;

    while (timer_ticks <= deadline) {
        if ((inb((uint16_t)(net_state.io_base + RTL_REG_CR)) & RTL_CR_RESET) == 0) return 0;
    }
    return -1;
}

int rtl8139_init_device() {
    pci_bar_info_t io_bar;
    pci_irq_route_t irq_route;
    pci_device_info_t nic;
    uint16_t command;

    memset(&net_state, 0, sizeof(net_state));
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(net_sockets, 0, sizeof(net_sockets));
    net_state.irq_line = 0xFFU;

    if (pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &nic) != 0) return -1;
    if (pci_decode_bar(&nic, 0, &io_bar) != 0 || !io_bar.is_io) return -1;

    command = pci_read16(nic.bus, nic.slot, nic.func, 0x04);
    command |= 0x0005U;
    command &= (uint16_t)~0x0400U;
    pci_write16(nic.bus, nic.slot, nic.func, 0x04, command);

    net_state.present = 1;
    net_state.pci_bus = nic.bus;
    net_state.pci_slot = nic.slot;
    net_state.pci_func = nic.func;
    net_state.io_base = (uint32_t)io_bar.base;
    if (pci_enable_irq(&nic, &irq_route) == 0) {
        net_state.irq_line = irq_route.irq_line;
        net_state.irq_pin = irq_route.irq_pin;
        net_state.irq_enabled = 1;
    } else if (pci_decode_irq(&nic, &irq_route) == 0) {
        net_state.irq_line = irq_route.routed ? irq_route.irq_line : 0xFFU;
        net_state.irq_pin = irq_route.irq_pin;
    }
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

    if (!net_state.present || !frame || length == 0U || length > RTL_TX_BUFFER_SIZE) return -1;

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
        if ((status & RTL_TSD_TOK) != 0U) {
            net_state.tx_index = (uint8_t)((index + 1U) & 0x03U);
            return 0;
        }
        if ((status & (RTL_TSD_TABT | RTL_TSD_TUN)) != 0U) return -1;
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

static int net_send_ethernet(const uint8_t* dst_mac, uint16_t ether_type,
                             const void* payload, uint16_t payload_len) {
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

static int net_send_arp_packet(uint16_t opcode, const uint8_t* target_mac,
                               uint32_t sender_ip, uint32_t target_ip) {
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

void net_clear_pending_udp() {
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

static void net_handle_frame(const uint8_t* frame, uint16_t frame_len);

void rtl8139_poll_receive() {
    while (net_state.present && ((inb((uint16_t)(net_state.io_base + RTL_REG_CR)) & RTL_CR_BUFE) == 0U)) {
        uint8_t* packet = rtl_rx_buffer + net_state.rx_offset;
        uint16_t status = (uint16_t)(packet[0] | ((uint16_t)packet[1] << 8));
        uint16_t packet_len = (uint16_t)(packet[2] | ((uint16_t)packet[3] << 8));
        uint16_t payload_len;

        if ((status & 0x0001U) == 0U || packet_len < 4U || packet_len > 1600U) {
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

int net_wait_until(int* status_ptr, uint32_t timeout_ticks) {
    uint32_t deadline = timer_ticks + timeout_ticks;

    while (timer_ticks <= deadline) {
        rtl8139_poll_receive();
        net_pump_ui();
        if (*status_ptr != -1) return 0;
        asm volatile("hlt");
    }
    return -1;
}

int net_ensure_configured() {
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

static int net_send_ipv4_packet(uint32_t src_ip, uint32_t dst_ip, uint8_t protocol,
                                const void* payload, uint16_t payload_len,
                                const uint8_t* forced_dst_mac) {
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

int net_send_udp(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                 const void* payload, uint16_t payload_len, const uint8_t* forced_dst_mac) {
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

int net_send_icmp_echo(uint32_t dst_ip, uint16_t identifier, uint16_t sequence) {
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
        payload[i] = (uint8_t)('A' + (i % 26U));
    }
    icmp->checksum = 0;
    icmp->checksum = net_htons(net_checksum16(packet, packet_len));

    return net_send_ipv4_packet(net_state.ip_addr, dst_ip, IPV4_PROTOCOL_ICMP, packet, packet_len, 0);
}

int net_send_tcp_segment(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
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
        net_secret_ready = 0;
        (void)net_run_dhcp(0);
    }
}

void net_poll() {
    rtl8139_poll_receive();
}
