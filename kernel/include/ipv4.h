#ifndef LIXOS_IPV4_H
#define LIXOS_IPV4_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "net.h"

// IP Protocol Numbers (RFC 790 / IANA)
constexpr uint8_t IP_PROTO_ICMP = 1;
constexpr uint8_t IP_PROTO_TCP  = 6;
constexpr uint8_t IP_PROTO_UDP  = 17;

// RFC 791 IPv4 Header (20 bytes standard, without options)
struct __attribute__((packed)) ipv4_header {
    uint8_t  version_ihl;    // Version (4 bits = 4) + IHL (4 bits = 5 for 20 bytes) -> 0x45
    uint8_t  tos;            // Type of Service / DSCP+ECN (0x00)
    uint16_t total_length;   // Total Length (Header + Payload) in bytes (Big-endian)
    uint16_t identification; // Packet ID for fragmentation
    uint16_t flags_fragment; // Flags (3 bits) + Fragment Offset (13 bits)
    uint8_t  ttl;            // Time to Live (e.g., 64)
    uint8_t  protocol;       // IP_PROTO_ICMP, IP_PROTO_TCP, IP_PROTO_UDP
    uint16_t checksum;       // 16-bit One's Complement Header Checksum (Big-endian)
    uint32_t src_ip;         // Source IPv4 address (Big-endian)
    uint32_t dst_ip;         // Destination IPv4 address (Big-endian)
};

// IPv4 Interface Functions
void ipv4_init(void);

// Calculate and verify 16-bit RFC 791 IPv4 header checksum
uint16_t ipv4_calculate_checksum(const ipv4_header *hdr);

// Encapsulate and transmit an IPv4 datagram to dst_ip (host byte order)
// Next hop resolution and MAC translation are automatically handled via ARP.
int ipv4_send(uint32_t dst_ip_host, uint8_t protocol, const void *payload, size_t payload_len);

// Process inbound IPv4 datagram from Ethernet frame
void ipv4_handle_packet(const void *data, size_t len);

#endif // LIXOS_IPV4_H

