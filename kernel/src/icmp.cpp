#include "icmp.h"
#include "ipv4.h"
#include "net.h"
#include "kprint.h"
#include "string.h"

static uint32_t g_icmp_requests_handled = 0;
static uint32_t g_icmp_replies_received = 0;

void icmp_init(void) {
    g_icmp_requests_handled = 0;
    g_icmp_replies_received = 0;
    kprintf("[ ICMP] ICMP Layer Initialized (Ping Echo Request / Echo Reply Active).\n");
}

void icmp_handle_packet(uint32_t src_ip_host, const void *data, size_t len) {
    if (!data || len < sizeof(icmp_header)) {
        return;
    }

    // Verify ICMP checksum over the entire ICMP message
    if (net_checksum(data, len) != 0) {
        kprintf("[ ICMP] Warning: Checksum failed for ICMP packet from %u.%u.%u.%u!\n",
                (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF);
        return;
    }

    const icmp_header *hdr = reinterpret_cast<const icmp_header *>(data);

    if (hdr->type == ICMP_TYPE_ECHO_REQUEST) {
        g_icmp_requests_handled++;

        // Echo Request received: mirror payload back as Echo Reply
        uint8_t reply_buf[1500];
        if (len > sizeof(reply_buf)) return;

        memcpy(reply_buf, data, len);
        icmp_header *reply_hdr = reinterpret_cast<icmp_header *>(reply_buf);
        reply_hdr->type = ICMP_TYPE_ECHO_REPLY;
        reply_hdr->code = 0;
        reply_hdr->checksum = 0;
        reply_hdr->checksum = net_checksum(reply_buf, len);

        ipv4_send(src_ip_host, IP_PROTO_ICMP, reply_buf, len);

        kprintf("[ ICMP] Responded to Ping from %u.%u.%u.%u (ID: %u, Seq: %u, Len: %u) -> Echo Reply Sent.\n",
                (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                ntohs(hdr->identifier), ntohs(hdr->sequence_number),
                static_cast<unsigned int>(len));
    } else if (hdr->type == ICMP_TYPE_ECHO_REPLY) {
        g_icmp_replies_received++;
        kprintf("[ ICMP] Ping Success: Received Echo Reply from %u.%u.%u.%u (ID: %u, Seq: %u, Len: %u)\n",
                (src_ip_host >> 24) & 0xFF, (src_ip_host >> 16) & 0xFF,
                (src_ip_host >> 8) & 0xFF, src_ip_host & 0xFF,
                ntohs(hdr->identifier), ntohs(hdr->sequence_number),
                static_cast<unsigned int>(len));
    }
}

int icmp_send_echo_request(uint32_t dst_ip_host, uint16_t id, uint16_t seq, const void *payload, size_t len) {
    uint8_t buf[1500];
    size_t total_len = sizeof(icmp_header) + len;
    if (total_len > sizeof(buf)) {
        return -1;
    }

    icmp_header *hdr = reinterpret_cast<icmp_header *>(buf);
    hdr->type = ICMP_TYPE_ECHO_REQUEST;
    hdr->code = 0;
    hdr->checksum = 0;
    hdr->identifier = htons(id);
    hdr->sequence_number = htons(seq);

    if (len > 0 && payload) {
        memcpy(buf + sizeof(icmp_header), payload, len);
    }

    hdr->checksum = net_checksum(buf, total_len);

    kprintf("[ ICMP] Sending Ping (Echo Request) -> %u.%u.%u.%u (ID: %u, Seq: %u, %u bytes)...\n",
            (dst_ip_host >> 24) & 0xFF, (dst_ip_host >> 16) & 0xFF,
            (dst_ip_host >> 8) & 0xFF, dst_ip_host & 0xFF,
            id, seq, static_cast<unsigned int>(total_len));

    return ipv4_send(dst_ip_host, IP_PROTO_ICMP, buf, total_len);
}

uint32_t icmp_get_requests_handled(void) {
    return g_icmp_requests_handled;
}

uint32_t icmp_get_replies_received(void) {
    return g_icmp_replies_received;
}
