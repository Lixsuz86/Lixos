#ifndef LIXOS_SOCKET_H
#define LIXOS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "vfs.h"
#include "tcp.h"
#include "udp.h"

// Standard POSIX Address Families
constexpr int AF_INET = 2; // IPv4 Internet protocols

// Standard POSIX Socket Types
constexpr int SOCK_STREAM = 1; // Sequenced, reliable, two-way connection (TCP)
constexpr int SOCK_DGRAM  = 2; // Connectionless, unreliable datagrams (UDP)

// Standard POSIX Protocol Numbers
constexpr int IPPROTO_IP  = 0;
constexpr int IPPROTO_TCP = 6;
constexpr int IPPROTO_UDP = 17;

// Internet Address Structure (POSIX sockaddr_in)
struct sockaddr_in {
    uint16_t sin_family; // AF_INET
    uint16_t sin_port;   // 16-bit Port (Network byte order / Big-endian)
    uint32_t sin_addr;   // 32-bit IPv4 Address (Network byte order / Big-endian)
    char     sin_zero[8];
};

constexpr size_t MAX_KERNEL_SOCKETS = 32;
constexpr size_t SOCKET_RX_BUF_SIZE = 4096;

// Kernel Socket Representation
struct kernel_socket {
    bool            in_use;
    int             id;
    int             domain;
    int             type;
    int             protocol;
    tcp_pcb        *tcp;            // Active TCP connection block (if SOCK_STREAM)
    uint16_t        local_port;     // Host byte order
    uint16_t        remote_port;    // Host byte order
    uint32_t        remote_ip;      // Host byte order
    bool            is_listening;

    // Receive FIFO ring buffer for userspace reads
    uint8_t         rx_buffer[SOCKET_RX_BUF_SIZE];
    size_t          rx_head;
    size_t          rx_tail;
    size_t          rx_count;

    // VFS Node mapping for unified file descriptor I/O
    vfs_node        vnode;
    vfs_operations  vops;
};

// Socket Subsystem Management Functions
void socket_subsystem_init(void);

// Create a new socket object, returns internal socket ID (0..MAX-1) or negative error
int socket_create(int domain, int type, int protocol);

// Retrieve kernel socket by internal ID
kernel_socket *socket_get(int sock_id);

// Operations
int socket_bind(int sock_id, const sockaddr_in *addr);
int socket_connect(int sock_id, const sockaddr_in *addr);
int64_t socket_send(int sock_id, const void *buf, size_t len, int flags);
int64_t socket_recv(int sock_id, void *buf, size_t len, int flags);
int socket_close(int sock_id);

// Feed data received from network stack into socket receive buffer
void socket_deliver_data(int sock_id, const void *data, size_t len);

#endif // LIXOS_SOCKET_H
