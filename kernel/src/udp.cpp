#include "udp.h"
#include "ipv4.h"
#include "net.h"
#include "kprint.h"
#include "string.h"

struct udp_binding {
    uint16_t       port;
    udp_handler_fn handler;
    bool           active;
};

constexpr size_t MAX_UDP_BINDINGS = 32;
static udp_binding g_udp_bindings[MAX_UDP_BINDINGS];
static uint32_t    g_udp_rx_count = 0;

void udp_init(void) {
    memset(g_udp_bindings, 0, sizeof(g_udp_bindings));
    g_udp_rx_count = 0;
    kprintf("[ UDP ] UDP Layer Initialized. Port Dispatcher Ready.\n");
}

bool udp_bind_port(uint16_t port, udp_handler_fn handler) {
    if (!handler) return false;

    // Check if already bound
    for (size_t i = 0; i < MAX_UDP_BINDINGS; ++i) {
        if (g_udp_bindings[i].active && g_udp_bindings[i].port == port) {
            g_udp_bindings[i].handler = handler;
            return true;
        }
    }

    // Allocate free binding slot
    for (size_t i = 0; i < MAX_UDP_BINDINGS; ++i) {
        if (!g_udp_bindings[i].active) {
            g_udp_bindings[i].port = port;
            g_udp_bindings[i].handler = handler;
            g_udp_bindings[i].active = true;
            return true;
        }
    }

    return false;
}

int udp_send(uint32_t dst_ip_host, uint16_t src_port, uint16_t dst_port, const void *data, size_t len) {
    uint8_t buf[1500];
    size_t total_len = sizeof(udp_header) + len;
    if (total_len > sizeof(buf)) {
        return -1;
    }

    udp_header *hdr = reinterpret_cast<udp_header *>(buf);
    hdr->src_port = htons(src_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons(static_cast<uint16_t>(total_len));
    hdr->checksum = 0; // Checksum is optional in IPv4 (0 means unused)

    if (len > 0 && data) {
        memcpy(buf + sizeof(udp_header), data, len);
    }

    return ipv4_send(dst_ip_host, IP_PROTO_UDP, buf, total_len);
}

void udp_handle_packet(uint32_t src_ip_host, const void *data, size_t len) {
    if (!data || len < sizeof(udp_header)) {
        return;
    }

    const udp_header *hdr = reinterpret_cast<const udp_header *>(data);
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint16_t udp_len  = ntohs(hdr->length);

    size_t payload_len = 0;
    if (udp_len >= sizeof(udp_header) && udp_len <= len) {
        payload_len = udp_len - sizeof(udp_header);
    } else {
        payload_len = len - sizeof(udp_header);
    }

    const uint8_t *payload = reinterpret_cast<const uint8_t *>(data) + sizeof(udp_header);
    g_udp_rx_count++;

    // Find registered port listener
    for (size_t i = 0; i < MAX_UDP_BINDINGS; ++i) {
        if (g_udp_bindings[i].active && g_udp_bindings[i].port == dst_port) {
            g_udp_bindings[i].handler(src_ip_host, src_port, payload, payload_len);
            return;
        }
    }

    kprintf("[ UDP ] Datagram received on unbound port %u from %u.%u.%u.%u:%u (%u bytes payload)\n",
            dst_port,
            (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
            (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
            src_port, static_cast<unsigned int>(payload_len));
}

uint32_t udp_get_packets_received(void) {
    return g_udp_rx_count;
}
