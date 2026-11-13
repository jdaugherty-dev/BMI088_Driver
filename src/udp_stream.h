/*
 * Minimal fixed-size UDP sender for streaming IMU samples to the viewer.
 * No handshake, no retries: if nobody's listening the packets just vanish,
 * which is fine for a live dashboard (we only ever care about the latest
 * sample anyway).
 */
#ifndef UDP_STREAM_H
#define UDP_STREAM_H

#include <stdint.h>

#pragma pack(push, 1)
struct imu_udp_packet {
    uint32_t seq;
    uint64_t t_us; /* microseconds since program start (CLOCK_MONOTONIC) */
    float ax, ay, az; /* m/s^2, body frame */
    float gx, gy, gz; /* deg/s, body frame */
};
#pragma pack(pop)

struct udp_stream {
    int sock;
    int enabled;
};

/* dest_host may be NULL, in which case the stream is a no-op (enabled = 0). */
int udp_stream_init(struct udp_stream *s, const char *dest_host, uint16_t dest_port);
void udp_stream_send(struct udp_stream *s, const struct imu_udp_packet *pkt);
void udp_stream_close(struct udp_stream *s);

#endif /* UDP_STREAM_H */
