#ifndef WISN_PACKET
#define WISN_PACKET

#include <stdio.h>
#include <stdlib.h>

struct wisnPacket {
    unsigned long long timestamp;
    unsigned char mac[6];
    char rssi;
    unsigned short nodeNum;
    int x;
    int y;
} __attribute((packed));

void printPacket(struct wisnPacket *wisnData);

#endif
