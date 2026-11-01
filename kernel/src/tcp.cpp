#include "tcp.h"
#include "ipv4.h"
#include "net.h"
#include "kprint.h"
#include "string.h"

static tcp_pcb g_tcp_sockets[TCP_MAX_SOCKETS];

const char *tcp_state_to_string(TcpState state) {
    switch (state) {
        case TcpState::CLOSED:       return "CLOSED";
        case TcpState::LISTEN:       return "LISTEN";
        case TcpState::SYN_SENT:     return "SYN_SENT";
        case TcpState::SYN_RECEIVED: return "SYN_RECEIVED";
        case TcpState::ESTABLISHED:  return "ESTABLISHED";
        case TcpState::FIN_WAIT_1:   return "FIN_WAIT_1";
        case TcpState::FIN_WAIT_2:   return "FIN_WAIT_2";
        case TcpState::CLOSE_WAIT:   return "CLOSE_WAIT";
        case TcpState::CLOSING:      return "CLOSING";
        case TcpState::LAST_ACK:     return "LAST_ACK";
        case TcpState::TIME_WAIT:    return "TIME_WAIT";
        default:                     return "UNKNOWN";
    }
}

void tcp_init(void) {
    memset(g_tcp_sockets, 0, sizeof(g_tcp_sockets));
    kprintf("[ TCP ] TCP Subsystem Initialized. Three-Way Handshake & State Machine Active.\n");
}

static uint16_t tcp_calculate_checksum(uint32_t src_ip_net, uint32_t dst_ip_net,
                                       const void *tcp_segment, size_t segment_len) {
    uint8_t check_buf[1600];
    size_t total_len = sizeof(tcp_pseudo_header) + segment_len;
    if (total_len > sizeof(check_buf)) return 0;

    tcp_pseudo_header *pseudo = reinterpret_cast<tcp_pseudo_header *>(check_buf);
    pseudo->src_ip   = src_ip_net;
    pseudo->dst_ip   = dst_ip_net;
    pseudo->zero     = 0;
    pseudo->protocol = IP_PROTO_TCP;
    pseudo->tcp_len  = htons(static_cast<uint16_t>(segment_len));

    memcpy(check_buf + sizeof(tcp_pseudo_header), tcp_segment, segment_len);

    return net_checksum(check_buf, total_len);
}

tcp_pcb *tcp_listen(uint16_t local_port) {
    for (size_t i = 0; i < TCP_MAX_SOCKETS; ++i) {
        if (!g_tcp_sockets[i].active) {
            tcp_pcb &pcb = g_tcp_sockets[i];
            memset(&pcb, 0, sizeof(tcp_pcb));
            pcb.active      = true;
            pcb.state       = TcpState::LISTEN;
            pcb.local_port  = local_port;
            pcb.window_size = 8192;
            kprintf("[ TCP ] Socket %u listening on port %u (State: LISTEN)\n",
                    static_cast<unsigned int>(i), local_port);
            return &pcb;
        }
    }
    return nullptr;
}

tcp_pcb *tcp_connect(uint32_t remote_ip_host, uint16_t remote_port, uint16_t local_port) {
    for (size_t i = 0; i < TCP_MAX_SOCKETS; ++i) {
        if (!g_tcp_sockets[i].active) {
            tcp_pcb &pcb = g_tcp_sockets[i];
            memset(&pcb, 0, sizeof(tcp_pcb));
            pcb.active      = true;
            pcb.state       = TcpState::SYN_SENT;
            pcb.remote_ip   = remote_ip_host;
            pcb.remote_port = remote_port;
            pcb.local_port  = local_port;
            pcb.local_seq   = 10000 + static_cast<uint32_t>(i * 500);
            pcb.remote_ack  = 0;
            pcb.window_size = 8192;

            kprintf("[ TCP ] Active Open: Connecting to %u.%u.%u.%u:%u from local port %u (Sending SYN, Seq: %u)...\n",
                    (remote_ip_host >> 24) & 0xFF, (remote_ip_host >> 16) & 0xFF,
                    (remote_ip_host >> 8) & 0xFF, remote_ip_host & 0xFF,
                    remote_port, local_port, pcb.local_seq);

            // Transmit initial SYN segment
            tcp_send_segment(&pcb, TCP_FLAG_SYN, nullptr, 0);
            pcb.local_seq++; // SYN consumes 1 sequence number
            return &pcb;
        }
    }
    return nullptr;
}

int tcp_send_segment(tcp_pcb *pcb, uint16_t flags, const void *payload, size_t len) {
    if (!pcb || !pcb->active) return -1;

    uint8_t seg_buf[1500];
    size_t total_seg_len = sizeof(tcp_header) + len;
    if (total_seg_len > sizeof(seg_buf)) return -2;

    tcp_header *hdr = reinterpret_cast<tcp_header *>(seg_buf);
    hdr->src_port   = htons(pcb->local_port);
    hdr->dst_port   = htons(pcb->remote_port);
    hdr->seq_num    = htonl(pcb->local_seq);
    hdr->ack_num    = (flags & TCP_FLAG_ACK) ? htonl(pcb->remote_ack) : 0;
    
    // Data Offset: 5 32-bit words (20 bytes) in upper 4 bits
    uint16_t offset_and_flags = (5 << 12) | (flags & 0x003F);
    hdr->data_offset_flags = htons(offset_and_flags);
    hdr->window_size       = htons(pcb->window_size);
    hdr->checksum          = 0;
    hdr->urgent_ptr        = 0;

    if (len > 0 && payload) {
        memcpy(seg_buf + sizeof(tcp_header), payload, len);
    }

    const net_config &cfg = net_get_config();
    hdr->checksum = tcp_calculate_checksum(htonl(cfg.our_ip), htonl(pcb->remote_ip), seg_buf, total_seg_len);

    return ipv4_send(pcb->remote_ip, IP_PROTO_TCP, seg_buf, total_seg_len);
}

void tcp_handle_packet(uint32_t src_ip_host, const void *data, size_t len) {
    if (!data || len < sizeof(tcp_header)) return;

    const tcp_header *hdr = reinterpret_cast<const tcp_header *>(data);
    uint16_t src_port = ntohs(hdr->src_port);
    uint16_t dst_port = ntohs(hdr->dst_port);
    uint32_t seq_num  = ntohl(hdr->seq_num);
    uint32_t ack_num  = ntohl(hdr->ack_num);
    uint16_t flags    = ntohs(hdr->data_offset_flags) & 0x003F;
    uint8_t  header_len = ((ntohs(hdr->data_offset_flags) >> 12) & 0x0F) * 4;

    if (header_len < 20 || header_len > len) return;

    const uint8_t *payload = reinterpret_cast<const uint8_t *>(data) + header_len;
    size_t payload_len = len - header_len;

    // 1. Match against existing active sockets
    tcp_pcb *match_pcb = nullptr;
    for (size_t i = 0; i < TCP_MAX_SOCKETS; ++i) {
        if (!g_tcp_sockets[i].active) continue;

        // Exact 4-tuple match
        if (g_tcp_sockets[i].local_port == dst_port &&
            g_tcp_sockets[i].remote_port == src_port &&
            g_tcp_sockets[i].remote_ip == src_ip_host) {
            match_pcb = &g_tcp_sockets[i];
            break;
        }

        // Listener match (Wildcard remote)
        if (g_tcp_sockets[i].state == TcpState::LISTEN &&
            g_tcp_sockets[i].local_port == dst_port) {
            match_pcb = &g_tcp_sockets[i];
        }
    }

    if (!match_pcb) {
        // No socket matching: ignore or send RST (Defensive)
        return;
    }

    // 2. State Machine Transitions
    switch (match_pcb->state) {
        case TcpState::LISTEN:
            if (flags & TCP_FLAG_SYN) {
                // Incoming SYN on listening socket: Passive Open
                match_pcb->remote_ip   = src_ip_host;
                match_pcb->remote_port = src_port;
                match_pcb->remote_ack  = seq_num + 1;
                match_pcb->local_seq   = 50000;
                match_pcb->state       = TcpState::SYN_RECEIVED;

                kprintf("[ TCP ] Server: Incoming SYN from %u.%u.%u.%u:%u -> Responding with SYN-ACK (Seq: %u, Ack: %u)\n",
                        (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                        (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                        src_port, match_pcb->local_seq, match_pcb->remote_ack);

                tcp_send_segment(match_pcb, TCP_FLAG_SYN | TCP_FLAG_ACK, nullptr, 0);
                match_pcb->local_seq++; // SYN consumes 1 seq
            }
            break;

        case TcpState::SYN_SENT:
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                // Received SYN-ACK for our active connection request
                match_pcb->remote_ack = seq_num + 1;
                match_pcb->state      = TcpState::ESTABLISHED;

                kprintf("[ TCP ] Client: Received SYN-ACK from %u.%u.%u.%u:%u (Seq: %u, Ack: %u)\n",
                        (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                        (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                        src_port, seq_num, ack_num);
                kprintf("[ TCP ] Handshake Complete (Active): Sending ACK -> State: ESTABLISHED!\n");

                tcp_send_segment(match_pcb, TCP_FLAG_ACK, nullptr, 0);
            }
            break;

        case TcpState::SYN_RECEIVED:
            if (flags & TCP_FLAG_ACK) {
                // Final ACK of 3-way handshake received
                match_pcb->state = TcpState::ESTABLISHED;
                kprintf("[ TCP ] Handshake Complete (Passive): Final ACK received from %u.%u.%u.%u:%u -> State: ESTABLISHED!\n",
                        (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                        (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                        src_port);
            }
            break;

        case TcpState::ESTABLISHED:
            if (flags & TCP_FLAG_FIN) {
                // Peer requested close
                match_pcb->remote_ack = seq_num + 1;
                match_pcb->state      = TcpState::CLOSE_WAIT;
                kprintf("[ TCP ] Received FIN from %u.%u.%u.%u:%u -> Acknowledging and closing...\n",
                        (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                        (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                        src_port);
                tcp_send_segment(match_pcb, TCP_FLAG_ACK, nullptr, 0);
                tcp_close(match_pcb);
            } else if (payload_len > 0) {
                // Inbound data received in ESTABLISHED state
                size_t copy_bytes = (payload_len < (TCP_BUFFER_SIZE - match_pcb->rx_len)) ?
                                    payload_len : (TCP_BUFFER_SIZE - match_pcb->rx_len);
                memcpy(match_pcb->rx_buf + match_pcb->rx_len, payload, copy_bytes);
                match_pcb->rx_len += copy_bytes;
                match_pcb->remote_ack = seq_num + static_cast<uint32_t>(payload_len);

                kprintf("[ TCP ] Received %u bytes data from %u.%u.%u.%u:%u (Payload: \"%.*s\") -> Sending ACK\n",
                        static_cast<unsigned int>(payload_len),
                        (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                        (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                        src_port, static_cast<int>(copy_bytes > 30 ? 30 : copy_bytes),
                        reinterpret_cast<const char *>(payload));

                tcp_send_segment(match_pcb, TCP_FLAG_ACK, nullptr, 0);
            }
            break;

        case TcpState::FIN_WAIT_1:
            if (flags & TCP_FLAG_ACK) {
                match_pcb->state = TcpState::CLOSED;
                match_pcb->active = false;
                kprintf("[ TCP ] Connection closed cleanly (State: CLOSED).\n");
            }
            break;

        default:
            break;
    }
}

int tcp_close(tcp_pcb *pcb) {
    if (!pcb || !pcb->active) return -1;

    kprintf("[ TCP ] Closing connection to %u.%u.%u.%u:%u (Sending FIN-ACK)...\n",
            (pcb->remote_ip >> 24) & 0xFF, (pcb->remote_ip >> 16) & 0xFF,
            (pcb->remote_ip >> 8) & 0xFF, pcb->remote_ip & 0xFF,
            pcb->remote_port);

    tcp_send_segment(pcb, TCP_FLAG_FIN | TCP_FLAG_ACK, nullptr, 0);
    pcb->local_seq++;
    pcb->state = TcpState::FIN_WAIT_1;
    return 0;
}
