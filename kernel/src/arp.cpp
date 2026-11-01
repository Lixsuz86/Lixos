#include "arp.h"
#include "net.h"
#include "kprint.h"
#include "string.h"

static arp_cache_entry g_arp_table[ARP_CACHE_MAX_ENTRIES];
static uint32_t        g_arp_ticks = 0;

void arp_init(void) {
    memset(g_arp_table, 0, sizeof(g_arp_table));
    g_arp_ticks = 0;

    // Pre-populate standard QEMU SLIRP Gateway entry (10.0.2.2 -> 52:54:00:12:34:02)
    uint8_t qemu_gw_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x02};
    arp_cache_insert(make_ipv4(10, 0, 2, 2), qemu_gw_mac);

    kprintf("[ ARP ] ARP Subsystem Initialized. Cache capacity: %u entries.\n",
            static_cast<unsigned int>(ARP_CACHE_MAX_ENTRIES));
}

void arp_cache_insert(uint32_t ip_host, const uint8_t mac[6]) {
    g_arp_ticks++;

    // 1. Check if IP already exists in cache
    for (size_t i = 0; i < ARP_CACHE_MAX_ENTRIES; ++i) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == ip_host) {
            memcpy(g_arp_table[i].mac, mac, 6);
            g_arp_table[i].timestamp = g_arp_ticks;
            return;
        }
    }

    // 2. Find an empty slot
    for (size_t i = 0; i < ARP_CACHE_MAX_ENTRIES; ++i) {
        if (!g_arp_table[i].valid) {
            g_arp_table[i].ip = ip_host;
            memcpy(g_arp_table[i].mac, mac, 6);
            g_arp_table[i].valid = true;
            g_arp_table[i].timestamp = g_arp_ticks;
            return;
        }
    }

    // 3. Cache full: Replace oldest entry (LRU-like)
    size_t oldest_idx = 0;
    uint32_t min_tick = g_arp_table[0].timestamp;
    for (size_t i = 1; i < ARP_CACHE_MAX_ENTRIES; ++i) {
        if (g_arp_table[i].timestamp < min_tick) {
            min_tick = g_arp_table[i].timestamp;
            oldest_idx = i;
        }
    }

    g_arp_table[oldest_idx].ip = ip_host;
    memcpy(g_arp_table[oldest_idx].mac, mac, 6);
    g_arp_table[oldest_idx].valid = true;
    g_arp_table[oldest_idx].timestamp = g_arp_ticks;
}

bool arp_resolve(uint32_t target_ip_host, uint8_t out_mac[6]) {
    // Check if target is broadcast
    if (target_ip_host == 0xFFFFFFFF) {
        memset(out_mac, 0xFF, 6);
        return true;
    }

    for (size_t i = 0; i < ARP_CACHE_MAX_ENTRIES; ++i) {
        if (g_arp_table[i].valid && g_arp_table[i].ip == target_ip_host) {
            memcpy(out_mac, g_arp_table[i].mac, 6);
            g_arp_table[i].timestamp = ++g_arp_ticks;
            return true;
        }
    }

    // Not in cache -> broadcast ARP Request
    arp_send_request(target_ip_host);
    return false;
}

void arp_send_request(uint32_t target_ip_host) {
    arp_packet pkt{};
    pkt.hw_type    = htons(ARP_HW_ETHERNET);
    pkt.proto_type = htons(ARP_PROTO_IPV4);
    pkt.hw_len     = 6;
    pkt.proto_len  = 4;
    pkt.opcode     = htons(ARP_OP_REQUEST);

    const net_config &cfg = net_get_config();
    memcpy(pkt.sender_mac, cfg.our_mac.bytes, 6);
    pkt.sender_ip = htonl(cfg.our_ip);

    memset(pkt.target_mac, 0x00, 6);
    pkt.target_ip = htonl(target_ip_host);

    uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    net_send_ethernet(broadcast_mac, ETHERTYPE_ARP, &pkt, sizeof(pkt));
}

void arp_send_reply(uint32_t target_ip_host, const uint8_t target_mac[6]) {
    arp_packet pkt{};
    pkt.hw_type    = htons(ARP_HW_ETHERNET);
    pkt.proto_type = htons(ARP_PROTO_IPV4);
    pkt.hw_len     = 6;
    pkt.proto_len  = 4;
    pkt.opcode     = htons(ARP_OP_REPLY);

    const net_config &cfg = net_get_config();
    memcpy(pkt.sender_mac, cfg.our_mac.bytes, 6);
    pkt.sender_ip = htonl(cfg.our_ip);

    memcpy(pkt.target_mac, target_mac, 6);
    pkt.target_ip = htonl(target_ip_host);

    net_send_ethernet(target_mac, ETHERTYPE_ARP, &pkt, sizeof(pkt));
}

void arp_handle_packet(const void *data, size_t len) {
    if (!data || len < sizeof(arp_packet)) {
        return;
    }

    const arp_packet *pkt = reinterpret_cast<const arp_packet *>(data);

    if (ntohs(pkt->hw_type) != ARP_HW_ETHERNET ||
        ntohs(pkt->proto_type) != ARP_PROTO_IPV4 ||
        pkt->hw_len != 6 || pkt->proto_len != 4) {
        return;
    }

    uint16_t opcode = ntohs(pkt->opcode);
    uint32_t sender_ip = ntohl(pkt->sender_ip);
    uint32_t target_ip = ntohl(pkt->target_ip);

    // Record sender in ARP cache
    arp_cache_insert(sender_ip, pkt->sender_mac);

    const net_config &cfg = net_get_config();

    if (opcode == ARP_OP_REQUEST) {
        // Someone is asking for our MAC address
        if (target_ip == cfg.our_ip) {
            arp_send_reply(sender_ip, pkt->sender_mac);
            kprintf("[ ARP ] Answered ARP Request from %u.%u.%u.%u (Replied with our MAC)\n",
                    (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                    (sender_ip >> 8) & 0xFF, sender_ip & 0xFF);
        }
    } else if (opcode == ARP_OP_REPLY) {
        kprintf("[ ARP ] Received ARP Reply: %u.%u.%u.%u is at %02X:%02X:%02X:%02X:%02X:%02X\n",
                (sender_ip >> 24) & 0xFF, (sender_ip >> 16) & 0xFF,
                (sender_ip >> 8) & 0xFF, sender_ip & 0xFF,
                pkt->sender_mac[0], pkt->sender_mac[1], pkt->sender_mac[2],
                pkt->sender_mac[3], pkt->sender_mac[4], pkt->sender_mac[5]);
    }
}

size_t arp_cache_count(void) {
    size_t cnt = 0;
    for (size_t i = 0; i < ARP_CACHE_MAX_ENTRIES; ++i) {
        if (g_arp_table[i].valid) cnt++;
    }
    return cnt;
}

const arp_cache_entry *arp_cache_get_entries(void) {
    return g_arp_table;
}
