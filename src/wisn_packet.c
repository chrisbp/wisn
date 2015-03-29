#include "wisn_packet.h"

void printPacket(struct wisnPacket *wisnData) {
    printf("Node: %d, Time: %llu, MAC: %02X:%02X:%02X:%02X:%02X:%02X, RSSI: %f\n",
            wisnData->nodeNum, wisnData->timestamp, wisnData->mac[0], wisnData->mac[1],
            wisnData->mac[2], wisnData->mac[3], wisnData->mac[4], wisnData->mac[5],
            wisnData->rssi);
}

struct wisnPacket *clonePacket(struct wisnPacket *packet) {
    struct wisnPacket *newPacket = malloc(sizeof(struct wisnPacket));
    newPacket->timestamp = packet->timestamp;
    memcpy(newPacket->mac, packet->mac, ARRAY_SIZE(packet->mac));
    newPacket->rssi = packet->rssi;
    newPacket->nodeNum = packet->nodeNum;
    return newPacket;
}
