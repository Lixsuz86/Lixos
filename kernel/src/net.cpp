#include "net.h"
#include "arp.h"
#include "ipv4.h"
#include "kprint.h"
#include "string.h"

static net_config g_net_cfg{};
static bool       g_net_initialized = false;

void net_init(void) {
    if (g_net_initialized) return;

    // Default configuration for QEMU SLIRP user network
    g_net_cfg.our_ip      = make_ipv4(10, 0, 2, 15);
    g_net_cfg.gateway_ip  = make_ipv4(10, 0, 2, 2);
    g_net_cfg.subnet_mask = make_ipv4(255, 255, 255, 0);
    g_net_cfg.dns_ip      = make_ipv4(10, 0, 2, 3);
    g_net_cfg.our_mac     = e1000_get_mac();

    g_net_initialized = true;

    kprintf("[ NET ] Core Network Stack Initialized:\n");
    kprintf("        IPv4: 10.0.2.15 | Netmask: 255.255.255.0 | Gateway: 10.0.2.2\n");
    kprintf("        MAC : %02X:%02X:%02X:%02X:%02X:%02X\n",
            g_net_cfg.our_mac.bytes[0], g_net_cfg.our_mac.bytes[1], g_net_cfg.our_mac.bytes[2],
            g_net_cfg.our_mac.bytes[3], g_net_cfg.our_mac.bytes[4], g_net_cfg.our_mac.bytes[5]);
}

const net_config &net_get_config(void) {
    return g_net_cfg;
}

uint16_t net_checksum(const void *data, size_t len) {
    const uint16_t *ptr = reinterpret_cast<const uint16_t *>(data);
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }

    if (len == 1) {
        // If odd length, add padded last byte
        sum += static_cast<uint16_t>(*reinterpret_cast<const uint8_t *>(ptr));
    }

    // Fold 32-bit sum into 16 bits
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return static_cast<uint16_t>(~sum);
}

int net_send_ethernet(const uint8_t dest_mac[6], uint16_t ethertype, const void *payload, size_t len) {
    if (!dest_mac || !payload || len == 0 || (len + sizeof(ethernet_header)) > E1000_BUFFER_SIZE) {
        return -1;
    }

    uint8_t frame_buf[E1000_BUFFER_SIZE];
    ethernet_header *hdr = reinterpret_cast<ethernet_header *>(frame_buf);

    memcpy(hdr->dest_mac, dest_mac, 6);
    memcpy(hdr->src_mac, g_net_cfg.our_mac.bytes, 6);
    hdr->ethertype = htons(ethertype);

    memcpy(frame_buf + sizeof(ethernet_header), payload, len);
    size_t total_len = sizeof(ethernet_header) + len;

    // Minimum Ethernet payload padding to 60 bytes (excluding FCS which hardware adds)
    if (total_len < 60) {
        memset(frame_buf + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    return e1000_send_packet(frame_buf, total_len);
}

void net_handle_ethernet(const void *frame_data, size_t len) {
    if (!frame_data || len < sizeof(ethernet_header)) {
        return;
    }

    const ethernet_header *hdr = reinterpret_cast<const ethernet_header *>(frame_data);

    // Verify Destination MAC: Accept only if addressed to our MAC or Ethernet Broadcast
    bool is_our_mac = (memcmp(hdr->dest_mac, g_net_cfg.our_mac.bytes, 6) == 0);
    bool is_broadcast = true;
    for (int i = 0; i < 6; ++i) {
        if (hdr->dest_mac[i] != 0xFF) {
            is_broadcast = false;
            break;
        }
    }

    if (!is_our_mac && !is_broadcast) {
        return; // Frame not intended for this host
    }

    uint16_t ethertype = ntohs(hdr->ethertype);
    const uint8_t *payload = reinterpret_cast<const uint8_t *>(frame_data) + sizeof(ethernet_header);
    size_t payload_len = len - sizeof(ethernet_header);

    switch (ethertype) {
        case ETHERTYPE_ARP:
            arp_handle_packet(payload, payload_len);
            break;
        case ETHERTYPE_IPV4:
            ipv4_handle_packet(payload, payload_len);
            break;
        default:
            // Unknown or unhandled EtherType
            break;
    }
}

void net_poll(void) {
    uint8_t rx_buf[E1000_BUFFER_SIZE];
    int bytes_read = 0;

    // Drain pending packets from NIC ring
    while ((bytes_read = e1000_receive_packet(rx_buf, sizeof(rx_buf))) > 0) {
        net_handle_ethernet(rx_buf, static_cast<size_t>(bytes_read));
    }
}
