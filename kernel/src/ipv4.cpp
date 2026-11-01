#include "ipv4.h"
#include "net.h"
#include "arp.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"
#include "kprint.h"
#include "string.h"

static uint16_t g_ip_id = 1000;

void ipv4_init(void) {
    g_ip_id = 1000;
    kprintf("[ IPv4] IPv4 Layer Initialized. RFC 791 Checksum & Routing Active.\n");
}

uint16_t ipv4_calculate_checksum(const ipv4_header *hdr) {
    // Checksum over the 20-byte IPv4 header with checksum field treated as 0
    ipv4_header temp = *hdr;
    temp.checksum = 0;
    return net_checksum(&temp, sizeof(ipv4_header));
}

int ipv4_send(uint32_t dst_ip_host, uint8_t protocol, const void *payload, size_t payload_len) {
    if (!payload && payload_len > 0) {
        return -1;
    }

    size_t total_ip_len = sizeof(ipv4_header) + payload_len;
    if (total_ip_len > (E1000_BUFFER_SIZE - sizeof(ethernet_header))) {
        kprintf("[ IPv4] Packet too large to transmit: %u bytes\n", static_cast<unsigned int>(total_ip_len));
        return -2;
    }

    const net_config &cfg = net_get_config();

    // Check for Loopback (127.0.0.0/8 or local IP)
    if (dst_ip_host == cfg.our_ip || ((dst_ip_host >> 24) == 127)) {
        uint8_t loop_buffer[E1000_BUFFER_SIZE];
        ipv4_header *hdr = reinterpret_cast<ipv4_header *>(loop_buffer);

        hdr->version_ihl    = 0x45;
        hdr->tos            = 0x00;
        hdr->total_length   = htons(static_cast<uint16_t>(total_ip_len));
        hdr->identification = htons(++g_ip_id);
        hdr->flags_fragment = htons(0x4000);
        hdr->ttl            = 64;
        hdr->protocol       = protocol;
        hdr->src_ip         = htonl(dst_ip_host == cfg.our_ip ? cfg.our_ip : make_ipv4(127, 0, 0, 1));
        hdr->dst_ip         = htonl(dst_ip_host);
        hdr->checksum       = 0;
        hdr->checksum       = ipv4_calculate_checksum(hdr);

        if (payload_len > 0) {
            memcpy(loop_buffer + sizeof(ipv4_header), payload, payload_len);
        }

        ipv4_handle_packet(loop_buffer, total_ip_len);
        return 0;
    }

    // Determine Next Hop IP
    uint32_t next_hop_ip = dst_ip_host;
    if (dst_ip_host == 0xFFFFFFFF) {
        next_hop_ip = 0xFFFFFFFF; // Broadcast
    } else if ((dst_ip_host & cfg.subnet_mask) != (cfg.our_ip & cfg.subnet_mask)) {
        // Outside local subnet: route via default gateway
        next_hop_ip = cfg.gateway_ip;
    }

    // Resolve Destination MAC via ARP
    uint8_t dst_mac[6];
    if (!arp_resolve(next_hop_ip, dst_mac)) {
        // If not in cache, wait briefly and poll for reply
        for (int i = 0; i < 50; ++i) {
            net_poll();
            if (arp_resolve(next_hop_ip, dst_mac)) break;
            for (int delay = 0; delay < 10000; ++delay) asm volatile("pause");
        }
        // Fallback to gateway MAC if still unresolved
        if (!arp_resolve(next_hop_ip, dst_mac)) {
            uint8_t qemu_gw[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x02};
            memcpy(dst_mac, qemu_gw, 6);
        }
    }

    uint8_t packet_buffer[E1000_BUFFER_SIZE];
    ipv4_header *hdr = reinterpret_cast<ipv4_header *>(packet_buffer);

    hdr->version_ihl    = 0x45; // Version 4, IHL 5 (20 bytes)
    hdr->tos            = 0x00;
    hdr->total_length   = htons(static_cast<uint16_t>(total_ip_len));
    hdr->identification = htons(++g_ip_id);
    hdr->flags_fragment = htons(0x4000); // Don't Fragment flag set
    hdr->ttl            = 64;
    hdr->protocol       = protocol;
    hdr->checksum       = 0;
    hdr->src_ip         = htonl(cfg.our_ip);
    hdr->dst_ip         = htonl(dst_ip_host);
    hdr->checksum       = ipv4_calculate_checksum(hdr);

    if (payload_len > 0) {
        memcpy(packet_buffer + sizeof(ipv4_header), payload, payload_len);
    }

    return net_send_ethernet(dst_mac, ETHERTYPE_IPV4, packet_buffer, total_ip_len);
}

void ipv4_handle_packet(const void *data, size_t len) {
    if (!data || len < sizeof(ipv4_header)) {
        return;
    }

    const ipv4_header *hdr = reinterpret_cast<const ipv4_header *>(data);

    // Verify IPv4 Version
    uint8_t version = (hdr->version_ihl >> 4) & 0x0F;
    if (version != 4) {
        return;
    }

    uint8_t ihl_bytes = (hdr->version_ihl & 0x0F) * 4;
    if (ihl_bytes < 20 || ihl_bytes > len) {
        return;
    }

    // Verify Checksum
    uint16_t calc_csum = net_checksum(hdr, ihl_bytes);
    if (calc_csum != 0) {
        kprintf("[ IPv4] Warning: Bad IPv4 header checksum! Discarding packet.\n");
        return;
    }

    uint32_t src_ip = ntohl(hdr->src_ip);
    uint32_t dst_ip = ntohl(hdr->dst_ip);

    const net_config &cfg = net_get_config();

    // Verify that packet is addressed to our IP, broadcast, subnet broadcast, or loopback
    bool is_for_us = (dst_ip == cfg.our_ip) ||
                     (dst_ip == 0xFFFFFFFF) ||
                     (dst_ip == (cfg.our_ip | ~cfg.subnet_mask)) ||
                     ((dst_ip >> 24) == 127);
    if (!is_for_us) {
        return; // Ignore packets not intended for this node
    }

    uint16_t total_len = ntohs(hdr->total_length);
    size_t payload_len = (total_len >= ihl_bytes && total_len <= len) ? (total_len - ihl_bytes) : (len - ihl_bytes);
    const uint8_t *payload = reinterpret_cast<const uint8_t *>(data) + ihl_bytes;

    switch (hdr->protocol) {
        case IP_PROTO_ICMP:
            icmp_handle_packet(src_ip, payload, payload_len);
            break;
        case IP_PROTO_UDP:
            udp_handle_packet(src_ip, payload, payload_len);
            break;
        case IP_PROTO_TCP:
            tcp_handle_packet(src_ip, payload, payload_len);
            break;
        default:
            // Unhandled IP protocol
            break;
    }
}
