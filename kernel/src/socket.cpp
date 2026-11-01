#include "socket.h"
#include "net.h"
#include "syscall.h"
#include "kprint.h"
#include "string.h"

static kernel_socket g_sockets[MAX_KERNEL_SOCKETS];
static uint16_t      g_next_ephemeral_port = 49152;
static bool          g_socket_subsystem_initialized = false;

// Forward declaration of VFS adapter callbacks
static int64_t socket_vfs_read(vfs_node *node, uint64_t offset, size_t size, void *buffer);
static int64_t socket_vfs_write(vfs_node *node, uint64_t offset, size_t size, const void *buffer);
static int     socket_vfs_close(vfs_node *node);

static int socket_get_id(const kernel_socket *sock) {
    if (!sock) return -1;
    for (size_t i = 0; i < MAX_KERNEL_SOCKETS; ++i) {
        if (&g_sockets[i] == sock) return static_cast<int>(i);
    }
    return -1;
}

void socket_subsystem_init(void) {
    if (g_socket_subsystem_initialized) return;

    memset(g_sockets, 0, sizeof(g_sockets));
    g_next_ephemeral_port = 49152;
    g_socket_subsystem_initialized = true;

    kprintf("[ SOCK] Socket Subsystem Initialized (Capacity: %u sockets, VFS Integration Active).\n",
            static_cast<unsigned int>(MAX_KERNEL_SOCKETS));
}

int socket_create(int domain, int type, int protocol) {
    if (!g_socket_subsystem_initialized) {
        socket_subsystem_init();
    }

    if (domain != AF_INET) {
        return -EINVAL;
    }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        return -EINVAL;
    }

    // Default protocol resolution
    if (protocol == IPPROTO_IP) {
        protocol = (type == SOCK_STREAM) ? IPPROTO_TCP : IPPROTO_UDP;
    }

    for (size_t i = 0; i < MAX_KERNEL_SOCKETS; ++i) {
        if (!g_sockets[i].in_use) {
            kernel_socket &sock = g_sockets[i];
            memset(&sock, 0, sizeof(kernel_socket));
            sock.in_use       = true;
            sock.id           = static_cast<int>(i);
            sock.domain       = domain;
            sock.type         = type;
            sock.protocol     = protocol;
            sock.tcp          = nullptr;
            sock.local_port   = 0;
            sock.remote_port  = 0;
            sock.remote_ip    = 0;
            sock.is_listening = false;

            // Configure VFS operations
            sock.vops.read    = socket_vfs_read;
            sock.vops.write   = socket_vfs_write;
            sock.vops.close   = socket_vfs_close;
            sock.vops.open    = nullptr;
            sock.vops.readdir = nullptr;
            sock.vops.finddir = nullptr;
            sock.vops.create  = nullptr;
            sock.vops.mkdir   = nullptr;
            sock.vops.truncate= nullptr;

            // Configure VFS node
            strncpy(sock.vnode.name, "socket", sizeof(sock.vnode.name));
            sock.vnode.type         = vfs_node_type::SOCKET;
            sock.vnode.mode         = 0666;
            sock.vnode.ops          = &sock.vops;
            sock.vnode.private_data = &sock;
            sock.vnode.ref_count    = 1;
            sock.vnode.length       = 0;
            sock.vnode.inode        = static_cast<uint32_t>(1000 + i);

            return static_cast<int>(i);
        }
    }

    return -ENOMEM;
}

kernel_socket *socket_get(int sock_id) {
    if (sock_id < 0 || sock_id >= static_cast<int>(MAX_KERNEL_SOCKETS)) {
        return nullptr;
    }
    if (!g_sockets[sock_id].in_use) {
        return nullptr;
    }
    return &g_sockets[sock_id];
}

int socket_bind(int sock_id, const sockaddr_in *addr) {
    kernel_socket *sock = socket_get(sock_id);
    if (!sock || !addr) {
        return -EINVAL;
    }

    uint16_t port = ntohs(addr->sin_port);
    sock->local_port = port;

    if (sock->type == SOCK_STREAM) {
        sock->tcp = tcp_listen(port);
        if (!sock->tcp) return -ENOMEM;
        sock->is_listening = true;
    } else if (sock->type == SOCK_DGRAM) {
        udp_bind_port(port, [](uint32_t src_ip, uint16_t src_port, const void *payload, size_t len) {
            (void)src_ip; (void)src_port;
            // Deliver to any matching UDP socket
            for (size_t i = 0; i < MAX_KERNEL_SOCKETS; ++i) {
                if (g_sockets[i].in_use && g_sockets[i].type == SOCK_DGRAM) {
                    socket_deliver_data(static_cast<int>(i), payload, len);
                }
            }
        });
    }

    return 0;
}

int socket_connect(int sock_id, const sockaddr_in *addr) {
    kernel_socket *sock = socket_get(sock_id);
    if (!sock || !addr) {
        return -EINVAL;
    }

    uint32_t target_ip   = ntohl(addr->sin_addr);
    uint16_t target_port = ntohs(addr->sin_port);
    uint16_t local_port  = sock->local_port ? sock->local_port : g_next_ephemeral_port++;

    sock->remote_ip   = target_ip;
    sock->remote_port = target_port;
    sock->local_port  = local_port;

    if (sock->type == SOCK_STREAM) {
        sock->tcp = tcp_connect(target_ip, target_port, local_port);
        if (!sock->tcp) {
            return -ECONNREFUSED;
        }

        // Drive network packet loop until connection completes or times out
        int attempts = 100;
        while (sock->tcp->state != TcpState::ESTABLISHED && attempts > 0) {
            net_poll();
            if (sock->tcp->state == TcpState::ESTABLISHED) break;
            attempts--;
            for (int delay = 0; delay < 2000; ++delay) asm volatile("pause");
        }

        if (sock->tcp->state != TcpState::ESTABLISHED) {
            return -ETIMEDOUT;
        }
    }

    return 0;
}

int64_t socket_send(int sock_id, const void *buf, size_t len, int flags) {
    (void)flags;
    kernel_socket *sock = socket_get(sock_id);
    if (!sock || !buf) {
        return -EINVAL;
    }

    if (sock->type == SOCK_STREAM) {
        if (!sock->tcp || sock->tcp->state != TcpState::ESTABLISHED) {
            return -ENOTCONN;
        }
        int res = tcp_send_segment(sock->tcp, TCP_FLAG_ACK | TCP_FLAG_PSH, buf, len);
        if (res == 0) {
            sock->tcp->local_seq += static_cast<uint32_t>(len);
            return static_cast<int64_t>(len);
        }
        return -1;
    } else if (sock->type == SOCK_DGRAM) {
        int res = udp_send(sock->remote_ip, sock->local_port, sock->remote_port, buf, len);
        return (res == 0) ? static_cast<int64_t>(len) : -1;
    }

    return -EINVAL;
}

int64_t socket_recv(int sock_id, void *buf, size_t len, int flags) {
    (void)flags;
    kernel_socket *sock = socket_get(sock_id);
    if (!sock || !buf || len == 0) {
        return -EINVAL;
    }

    // If TCP socket, poll network and transfer data from tcp_pcb rx_buf if present
    if (sock->type == SOCK_STREAM && sock->tcp) {
        if (sock->rx_count == 0 && sock->tcp->rx_len > 0) {
            socket_deliver_data(sock_id, sock->tcp->rx_buf, sock->tcp->rx_len);
            sock->tcp->rx_len = 0;
        }

        // If buffer is still empty, poll NIC RX ring
        for (int i = 0; i < 50 && sock->rx_count == 0; ++i) {
            net_poll();
            if (sock->tcp->rx_len > 0) {
                socket_deliver_data(sock_id, sock->tcp->rx_buf, sock->tcp->rx_len);
                sock->tcp->rx_len = 0;
                break;
            }
            for (int delay = 0; delay < 2000; ++delay) asm volatile("pause");
        }
    } else if (sock->type == SOCK_DGRAM) {
        for (int i = 0; i < 50 && sock->rx_count == 0; ++i) {
            net_poll();
            if (sock->rx_count > 0) break;
            for (int delay = 0; delay < 2000; ++delay) asm volatile("pause");
        }
    }

    if (sock->rx_count == 0) {
        return 0; // No data available
    }

    size_t to_read = (len < sock->rx_count) ? len : sock->rx_count;
    uint8_t *out = static_cast<uint8_t *>(buf);

    for (size_t i = 0; i < to_read; ++i) {
        out[i] = sock->rx_buffer[sock->rx_head];
        sock->rx_head = (sock->rx_head + 1) % SOCKET_RX_BUF_SIZE;
    }
    sock->rx_count -= to_read;

    return static_cast<int64_t>(to_read);
}

int socket_close(int sock_id) {
    kernel_socket *sock = socket_get(sock_id);
    if (!sock) {
        return -EBADF;
    }

    if (sock->type == SOCK_STREAM && sock->tcp) {
        tcp_close(sock->tcp);
        sock->tcp = nullptr;
    }

    sock->in_use = false;
    return 0;
}

void socket_deliver_data(int sock_id, const void *data, size_t len) {
    kernel_socket *sock = socket_get(sock_id);
    if (!sock || !data || len == 0) return;

    const uint8_t *src = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        if (sock->rx_count >= SOCKET_RX_BUF_SIZE) break; // Buffer full
        sock->rx_buffer[sock->rx_tail] = src[i];
        sock->rx_tail = (sock->rx_tail + 1) % SOCKET_RX_BUF_SIZE;
        sock->rx_count++;
    }
}

// -----------------------------------------------------------------------------
// VFS Adapter Callbacks
// -----------------------------------------------------------------------------

static int64_t socket_vfs_read(vfs_node *node, uint64_t offset, size_t size, void *buffer) {
    (void)offset;
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    int sock_id = socket_get_id(sock);
    return socket_recv(sock_id, buffer, size, 0);
}

static int64_t socket_vfs_write(vfs_node *node, uint64_t offset, size_t size, const void *buffer) {
    (void)offset;
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    int sock_id = socket_get_id(sock);
    return socket_send(sock_id, buffer, size, 0);
}

static int socket_vfs_close(vfs_node *node) {
    kernel_socket *sock = static_cast<kernel_socket *>(node->private_data);
    int sock_id = socket_get_id(sock);
    return socket_close(sock_id);
}
