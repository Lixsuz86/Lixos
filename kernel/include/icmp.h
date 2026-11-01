#ifndef LIXOS_ICMP_H
#define LIXOS_ICMP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ICMP Message Types (RFC 792)
constexpr uint8_t ICMP_TYPE_ECHO_REPLY   = 0;
constexpr uint8_t ICMP_TYPE_ECHO_REQUEST = 8;

// Standard RFC 792 ICMP Header (8 bytes)
struct __attribute__((packed)) icmp_header {
    uint8_t  type;            // Message Type (8 = Echo Request, 0 = Echo Reply)
    uint8_t  code;            // Message Sub-code (0)
    uint16_t checksum;        // 16-bit One's Complement Checksum of ICMP Header + Data
    uint16_t identifier;      // Session / Process Identifier (Big-endian)
    uint16_t sequence_number; // Sequence Number (Big-endian)
};

// ICMP Interface Functions
void icmp_init(void);

// Process inbound ICMP packet; automatically generates Echo Reply on Echo Request
void icmp_handle_packet(uint32_t src_ip_host, const void *data, size_t len);

// Construct and transmit an ICMP Echo Request (Ping) to dst_ip
int icmp_send_echo_request(uint32_t dst_ip_host, uint16_t id, uint16_t seq, const void *payload, size_t len);

// Statistics and Verification Counters
uint32_t icmp_get_requests_handled(void);
uint32_t icmp_get_replies_received(void);

#endif // LIXOS_ICMP_H

