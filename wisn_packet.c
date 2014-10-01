#include "wisn_packet.h"

void printPacket(struct wisnPacket *wisnData) {
    printf("Node: %d, X: %d, Y: %d, Time: %llu, MAC: %02X:%02X:%02X:%02X:%02X:%02X, RSSI: %d\n",
            wisnData->baseNum, wisnData->x, wisnData->y, wisnData->timestamp, wisnData->mac[0],
            wisnData->mac[1], wisnData->mac[2], wisnData->mac[3], wisnData->mac[4], wisnData->mac[5],
            wisnData->rssi);
}
