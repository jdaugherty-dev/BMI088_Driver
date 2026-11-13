#include "udp_stream.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr_in g_dest;

int udp_stream_init(struct udp_stream *s, const char *dest_host, uint16_t dest_port)
{
    s->sock = -1;
    s->enabled = 0;

    if (!dest_host) {
        return 0;
    }

    s->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&g_dest, 0, sizeof(g_dest));
    g_dest.sin_family = AF_INET;
    g_dest.sin_port = htons(dest_port);

    if (inet_pton(AF_INET, dest_host, &g_dest.sin_addr) != 1) {
        fprintf(stderr, "invalid destination IP: %s\n", dest_host);
        close(s->sock);
        s->sock = -1;
        return -1;
    }

    s->enabled = 1;
    return 0;
}

void udp_stream_send(struct udp_stream *s, const struct imu_udp_packet *pkt)
{
    if (!s->enabled) {
        return;
    }

    /* Best-effort: a dropped UDP datagram just means one missed frame on the viewer. */
    (void)sendto(s->sock, pkt, sizeof(*pkt), 0, (struct sockaddr *)&g_dest, sizeof(g_dest));
}

void udp_stream_close(struct udp_stream *s)
{
    if (s->sock >= 0) {
        close(s->sock);
        s->sock = -1;
    }
    s->enabled = 0;
}
