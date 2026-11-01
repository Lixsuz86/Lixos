#ifndef LIXOS_NET_H
#define LIXOS_NET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "e1000.h"

// Endianness Conversion Helpers (x86_64 Little-Endian <-> Network Big-Endian)
static inline uint16_t htons(uint16_t host16) {
    return static_cast<uint16_t>(((host16 & 0xFF) << 8) | ((host16 >> 8) & 0xFF));
}

static inline uint16_t ntohs(uint16_t net16) {
    return htons(net16);
}

static inline uint32_t htonl(uint32_t host32) {
    return ((host32 & 0x000000FFU) << 24) |
           ((host32 & 0x0000FF00U) << 8)  |
           ((host32 & 0x00FF0000U) >> 8)  |
           ((host32 & 0xFF000000U) >> 24);
}

static inline uint32_t ntohl(uint32_t net32) {
    return htonl(net32);
}

// IP Address Conversion Helper: e.g. make_ipv4(10, 0, 2, 15) -> 0x0A00020F (Host Order)
static inline constexpr uint32_t make_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return (static_cast<uint32_t>(a) << 24) |
           (static_cast<uint32_t>(b) << 16) |
           (static_cast<uint32_t>(c) << 8)  |
            static_cast<uint32_t>(d);
}

// Ethernet Protocol Constants (RFC 894 / IEEE 802.3)
constexpr uint16_t ETHERTYPE_IPV4 = 0x0800;
constexpr uint16_t ETHERTYPE_ARP  = 0x0806;

// Standard Ethernet II Frame Header (14 bytes)
struct __attribute__((packed)) ethernet_header {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype; // Big-endian
};

// Static Network Configuration for LixOS in QEMU SLIRP User Network
struct net_config {
    uint32_t    our_ip;      // 10.0.2.15 (Host byte order)
    uint32_t    gateway_ip;  // 10.0.2.2  (Host byte order)
    uint32_t    subnet_mask; // 255.255.255.0 (Host byte order)
    uint32_t    dns_ip;      // 10.0.2.3  (Host byte order)
    mac_address our_mac;     // 52:54:00:12:34:56
};

// Global Network Interface Functions
void net_init(void);
const net_config &net_get_config(void);

// Calculate RFC 1071 / RFC 791 16-bit One's Complement Internet Checksum
uint16_t net_checksum(const void *data, size_t len);

// Transmit Raw Ethernet Frame via e1000 Driver
int net_send_ethernet(const uint8_t dest_mac[6], uint16_t ethertype, const void *payload, size_t len);

// Process Inbound Ethernet Frame
void net_handle_ethernet(const void *frame_data, size_t len);

// Poll NIC RX Ring for Incoming Packets
void net_poll(void);

#endif // LIXOS_NET_H
