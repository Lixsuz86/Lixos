#ifndef LIXOS_E1000_H
#define LIXOS_E1000_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "pci.h"
#include "driver.h"

// PCI Identifiers for Intel Gigabit Ethernet
constexpr uint16_t E1000_VENDOR_INTEL = 0x8086;
constexpr uint16_t E1000_DEV_82540EM  = 0x100E; // QEMU default
constexpr uint16_t E1000_DEV_82545EM  = 0x100F;
constexpr uint16_t E1000_DEV_82543GC  = 0x1004;

// e1000 MMIO Register Offsets
constexpr uint32_t REG_CTRL      = 0x0000; // Device Control
constexpr uint32_t REG_STATUS    = 0x0008; // Device Status
constexpr uint32_t REG_EECD      = 0x0010; // EEPROM/Flash Control
constexpr uint32_t REG_EERD      = 0x0014; // EEPROM Read
constexpr uint32_t REG_CTRL_EXT  = 0x0018; // Extended Device Control
constexpr uint32_t REG_ICR       = 0x00C0; // Interrupt Cause Read
constexpr uint32_t REG_ITR       = 0x00C4; // Interrupt Throttling Rate
constexpr uint32_t REG_ICS       = 0x00C8; // Interrupt Cause Set
constexpr uint32_t REG_IMS       = 0x00D0; // Interrupt Mask Set/Read
constexpr uint32_t REG_IMC       = 0x00D8; // Interrupt Mask Clear
constexpr uint32_t REG_RCTL      = 0x0100; // Receive Control
constexpr uint32_t REG_TCTL      = 0x0400; // Transmit Control
constexpr uint32_t REG_TIPG      = 0x0410; // Transmit Inter-Packet Gap
constexpr uint32_t REG_RDBAL     = 0x2800; // RX Descriptor Base Address Low
constexpr uint32_t REG_RDBAH     = 0x2804; // RX Descriptor Base Address High
constexpr uint32_t REG_RDLEN     = 0x2808; // RX Descriptor Length
constexpr uint32_t REG_RDH       = 0x2810; // RX Descriptor Head
constexpr uint32_t REG_RDT       = 0x2818; // RX Descriptor Tail
constexpr uint32_t REG_TDBAL     = 0x3800; // TX Descriptor Base Address Low
constexpr uint32_t REG_TDBAH     = 0x3804; // TX Descriptor Base Address High
constexpr uint32_t REG_TDLEN     = 0x3808; // TX Descriptor Length
constexpr uint32_t REG_TDH       = 0x3810; // TX Descriptor Head
constexpr uint32_t REG_TDT       = 0x3818; // TX Descriptor Tail
constexpr uint32_t REG_MTA       = 0x5200; // Multicast Table Array (0x5200-0x53FC, 128 entries)
constexpr uint32_t REG_RAL0      = 0x5400; // Receive Address Low 0 (MAC bytes 0-3)
constexpr uint32_t REG_RAH0      = 0x5404; // Receive Address High 0 (MAC bytes 4-5 + Valid bit)

// Control Register Bits
constexpr uint32_t CTRL_FD       = (1 << 0);  // Full Duplex
constexpr uint32_t CTRL_LRST     = (1 << 3);  // Link Reset
constexpr uint32_t CTRL_SLU      = (1 << 6);  // Set Link Up
constexpr uint32_t CTRL_RST      = (1 << 26); // Device Reset

// Receive Control Bits
constexpr uint32_t RCTL_EN       = (1 << 1);  // Receiver Enable
constexpr uint32_t RCTL_SBP      = (1 << 2);  // Store Bad Packets
constexpr uint32_t RCTL_UPE      = (1 << 3);  // Unicast Promiscuous Enabled
constexpr uint32_t RCTL_MPE      = (1 << 4);  // Multicast Promiscuous Enabled
constexpr uint32_t RCTL_LPE      = (1 << 5);  // Long Packet Enable
constexpr uint32_t RCTL_BAM      = (1 << 15); // Broadcast Accept Mode
constexpr uint32_t RCTL_BSIZE_2048 = (0 << 16); // Buffer size 2048 bytes
constexpr uint32_t RCTL_SECRC    = (1 << 26); // Strip Ethernet CRC

// Transmit Control Bits
constexpr uint32_t TCTL_EN       = (1 << 1);  // Transmit Enable
constexpr uint32_t TCTL_PSP      = (1 << 3);  // Pad Short Packets
constexpr uint32_t TCTL_CT       = (0x0F << 4);  // Collision Threshold (15)
constexpr uint32_t TCTL_COLD     = (0x40 << 12); // Collision Distance (64)

// Descriptor Command Bits
constexpr uint8_t  TXD_CMD_EOP   = (1 << 0);  // End of Packet
constexpr uint8_t  TXD_CMD_IFCS  = (1 << 1);  // Insert FCS/CRC
constexpr uint8_t  TXD_CMD_IC    = (1 << 2);  // Insert Checksum
constexpr uint8_t  TXD_CMD_RS    = (1 << 3);  // Report Status

// Descriptor Status Bits
constexpr uint8_t  TXD_STAT_DD   = (1 << 0);  // Descriptor Done
constexpr uint8_t  RXD_STAT_DD   = (1 << 0);  // Descriptor Done
constexpr uint8_t  RXD_STAT_EOP  = (1 << 1);  // End of Packet

// Ring Buffer Configurations
constexpr size_t E1000_NUM_RX_DESC = 32;
constexpr size_t E1000_NUM_TX_DESC = 32;
constexpr size_t E1000_BUFFER_SIZE  = 2048;

// Hardware Descriptors (aligned to 16 bytes by spec)
struct __attribute__((packed, aligned(16))) e1000_rx_desc {
    uint64_t buffer_addr; // Physical address of receive buffer
    uint16_t length;      // Length of packet received
    uint16_t checksum;    // Packet checksum
    uint8_t  status;      // Descriptor status
    uint8_t  errors;      // Descriptor errors
    uint16_t special;
};

struct __attribute__((packed, aligned(16))) e1000_tx_desc {
    uint64_t buffer_addr; // Physical address of transmit buffer
    uint16_t length;      // Length of packet to transmit
    uint8_t  cso;         // Checksum offset
    uint8_t  cmd;         // Command flags (EOP, IFCS, RS)
    uint8_t  status;      // Status flags (DD)
    uint8_t  css;         // Checksum start
    uint16_t special;
};

struct mac_address {
    uint8_t bytes[6];
};

// API Functions
bool e1000_init(void);
bool e1000_is_available(void);
mac_address e1000_get_mac(void);

// Send / Receive raw Ethernet frames
int e1000_send_packet(const void *packet, size_t length);
int e1000_receive_packet(void *out_buffer, size_t max_len);

// Generic driver abstraction interface
const driver *e1000_get_driver(void);

#endif // LIXOS_E1000_H
