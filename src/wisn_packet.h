#ifndef WISN_PACKET
#define WISN_PACKET

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct wisnPacket {
    unsigned long long timestamp;
    unsigned char mac[6];
    double rssi;
    unsigned short nodeNum;
} __attribute((packed));

void printPacket(struct wisnPacket *wisnData);
struct wisnPacket *clonePacket(struct wisnPacket *packet);

#endif
