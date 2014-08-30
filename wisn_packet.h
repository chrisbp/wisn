#ifndef WISN_PACKET
#define WISN_PACKET

#define PORT "1245"
#define PACKET 0

struct wisnPacket {
    unsigned long long timestamp;
    unsigned char mac[6];
    char rssi;
    unsigned short baseNum;
} __attribute((packed));

#endif
