#ifndef LIXOS_ARP_H
#define LIXOS_ARP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// ARP Constants (RFC 826)
constexpr uint16_t ARP_HW_ETHERNET = 1;
constexpr uint16_t ARP_PROTO_IPV4  = 0x0800;
constexpr uint16_t ARP_OP_REQUEST  = 1;
constexpr uint16_t ARP_OP_REPLY    = 2;

// Standard RFC 826 ARP Packet Structure (28 bytes)
struct __attribute__((packed)) arp_packet {
    uint16_t hw_type;       // Hardware Type (1 for Ethernet, Big-endian)
    uint16_t proto_type;    // Protocol Type (0x0800 for IPv4, Big-endian)
    uint8_t  hw_len;        // Hardware address length (6 for MAC)
    uint8_t  proto_len;     // Protocol address length (4 for IPv4)
    uint16_t opcode;        // Opcode: 1 = Request, 2 = Reply (Big-endian)
    uint8_t  sender_mac[6]; // Hardware address of sender
    uint32_t sender_ip;     // IPv4 address of sender (Big-endian)
    uint8_t  target_mac[6]; // Hardware address of target
    uint32_t target_ip;     // IPv4 address of target (Big-endian)
};

// Dynamic ARP Cache Entry
struct arp_cache_entry {
    uint32_t ip;            // IP in host byte order
    uint8_t  mac[6];        // Resolved MAC address
    bool     valid;         // Slot active flag
    uint32_t timestamp;     // Last accessed / updated tick
};

constexpr size_t ARP_CACHE_MAX_ENTRIES = 32;

// ARP Functions
void arp_init(void);
void arp_handle_packet(const void *data, size_t len);

// Lookup IP in ARP cache. If not found, broadcasts an ARP request.
// Returns true if resolved (and sets out_mac), false if pending resolution.
bool arp_resolve(uint32_t target_ip_host, uint8_t out_mac[6]);

// Construct and transmit ARP Request broadcast (FF:FF:FF:FF:FF:FF)
void arp_send_request(uint32_t target_ip_host);

// Construct and transmit targeted ARP Reply
void arp_send_reply(uint32_t target_ip_host, const uint8_t target_mac[6]);

// Direct insertion into ARP cache
void arp_cache_insert(uint32_t ip_host, const uint8_t mac[6]);

// Query cache statistics
size_t arp_cache_count(void);
const arp_cache_entry *arp_cache_get_entries(void);

#endif // LIXOS_ARP_H
