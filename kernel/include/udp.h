#ifndef LIXOS_UDP_H
#define LIXOS_UDP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// RFC 768 UDP Header (8 bytes)
struct __attribute__((packed)) udp_header {
    uint16_t src_port; // Source Port (Big-endian)
    uint16_t dst_port; // Destination Port (Big-endian)
    uint16_t length;   // Length of UDP Header + Payload (Big-endian)
    uint16_t checksum; // Checksum (Big-endian, optional in IPv4)
};

// Callback signature for UDP port listeners
using udp_handler_fn = void (*)(uint32_t src_ip_host, uint16_t src_port, const void *payload, size_t len);

// UDP Interface Functions
void udp_init(void);

// Register a handler callback for a specific local UDP port
bool udp_bind_port(uint16_t port, udp_handler_fn handler);

// Transmit a UDP datagram to target IP and port
int udp_send(uint32_t dst_ip_host, uint16_t src_port, uint16_t dst_port, const void *data, size_t len);

// Process inbound UDP packet
void udp_handle_packet(uint32_t src_ip_host, const void *data, size_t len);

// Statistics and verification counters
uint32_t udp_get_packets_received(void);

#endif // LIXOS_UDP_H

