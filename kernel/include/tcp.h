#ifndef LIXOS_TCP_H
#define LIXOS_TCP_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// TCP Control Flags (RFC 793)
constexpr uint16_t TCP_FLAG_FIN = 0x0001;
constexpr uint16_t TCP_FLAG_SYN = 0x0002;
constexpr uint16_t TCP_FLAG_RST = 0x0004;
constexpr uint16_t TCP_FLAG_PSH = 0x0008;
constexpr uint16_t TCP_FLAG_ACK = 0x0010;
constexpr uint16_t TCP_FLAG_URG = 0x0020;

// Standard TCP State Machine Enumeration
enum class TcpState {
    CLOSED,
    LISTEN,
    SYN_SENT,
    SYN_RECEIVED,
    ESTABLISHED,
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSE_WAIT,
    CLOSING,
    LAST_ACK,
    TIME_WAIT
};

// RFC 793 TCP Header Structure (20 bytes standard without options)
struct __attribute__((packed)) tcp_header {
    uint16_t src_port;           // Source Port (Big-endian)
    uint16_t dst_port;           // Destination Port (Big-endian)
    uint32_t seq_num;            // Sequence Number (Big-endian)
    uint32_t ack_num;            // Acknowledgment Number (Big-endian)
    uint16_t data_offset_flags;  // Data Offset (4 bits) + Reserved (6 bits) + Control Flags (6 bits)
    uint16_t window_size;        // Flow Control Receive Window Size (Big-endian)
    uint16_t checksum;           // Header + Payload + Pseudo-header Checksum
    uint16_t urgent_ptr;         // Urgent Pointer (Big-endian)
};

// TCP Pseudo-Header for Checksum Calculation (12 bytes)
struct __attribute__((packed)) tcp_pseudo_header {
    uint32_t src_ip;    // Source IP (Big-endian)
    uint32_t dst_ip;    // Destination IP (Big-endian)
    uint8_t  zero;      // Always 0
    uint8_t  protocol;  // IP_PROTO_TCP (6)
    uint16_t tcp_len;   // TCP Header length + Payload length (Big-endian)
};

constexpr size_t TCP_MAX_SOCKETS = 16;
constexpr size_t TCP_BUFFER_SIZE = 2048;

// TCP Protocol Control Block (Socket State Representation)
struct tcp_pcb {
    bool        active;
    TcpState    state;
    uint32_t    remote_ip;       // Host byte order
    uint16_t    local_port;      // Host byte order
    uint16_t    remote_port;     // Host byte order
    uint32_t    local_seq;       // Next sequence number to send
    uint32_t    remote_ack;      // Next sequence number expected from remote
    uint16_t    window_size;     // Local receive window
    uint8_t     rx_buf[TCP_BUFFER_SIZE];
    size_t      rx_len;
};

// TCP Interface Functions
void tcp_init(void);

// Create a listening TCP socket on specified local port (Server role)
tcp_pcb *tcp_listen(uint16_t local_port);

// Initiate connection to remote IP:port (Client role, sends SYN)
tcp_pcb *tcp_connect(uint32_t remote_ip_host, uint16_t remote_port, uint16_t local_port);

// Transmit TCP segment with specific control flags and optional data
int tcp_send_segment(tcp_pcb *pcb, uint16_t flags, const void *payload, size_t len);

// Process inbound TCP segment
void tcp_handle_packet(uint32_t src_ip_host, const void *data, size_t len);

// Gracefully close connection (sends FIN)
int tcp_close(tcp_pcb *pcb);

// Convert TCP state to human-readable string for logging
const char *tcp_state_to_string(TcpState state);

#endif // LIXOS_TCP_H

