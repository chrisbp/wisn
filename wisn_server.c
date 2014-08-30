#include "wisn_server.h"

int listenFD;
struct linkedList connectionList;
volatile char isOpen;

int main(int argc, char *argv[]) {
    struct sigaction sa;
    struct wisnConnection *conn;
    struct linkedNode *node;
    socklen_t addrSize;
    pthread_attr_t attr;

    //Setup signal handler
    sa.sa_handler = cleanup;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    listenFD = createListenSocket(PORT);
    if (listenFD < 0) {
        exit(1);
    }

    if (listen(listenFD, 20)) {
        fprintf(stderr, "Error listening on socket.\n");
    }

    isOpen = 1;

    initList(&connectionList);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    while (isOpen) {
        conn = calloc(1, sizeof(*conn));
        addrSize = sizeof(conn->sockaddr);
        conn->sockFD = accept(listenFD, (struct sockaddr *)&(conn->sockaddr), &addrSize);
        if (conn->sockFD < 0) {
            fprintf(stderr, "Error accepting connection.\n");
            free(conn);
        } else {
            node = addConnectionToTailList(&connectionList, conn);
            //start new thread here
            pthread_create(&(conn->thread), &attr, readPackets, (void *)node);
        }
    }
    pthread_attr_destroy(&attr);
}

int createListenSocket(char *port) {
    int status;
    int sockFD = -1;
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *r;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    status = getaddrinfo(NULL, port, &hints, &results);
    if (status) {
        fprintf(stderr, "Error getting address info.\n");
        return status;
    }

    for (r = results; r != NULL; r = r->ai_next) {
        sockFD = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
        if (sockFD < 0) {
            fprintf(stderr, "Error creating socket.\n");
            freeaddrinfo(results);
            return -1;
        }
        status = bind(sockFD, r->ai_addr, r->ai_addrlen);
        if (status) {
            close(sockFD);
            fprintf(stderr, "Error binding socket.\n");
        } else {
            break;
        }
    }

    freeaddrinfo(results);

    if (status) {
        fprintf(stderr, "Error binding to port...exiting.\n");
    }

    return sockFD;
}

void cleanup() {
    isOpen = 0;
    close(listenFD);
    destroyList(&connectionList, CONNECTION);
}

void *readPackets(void *arg) {
    struct wisnPacket packet;
    unsigned char size = sizeof(struct wisnPacket);
    unsigned char buffer[size];
    struct linkedNode *node = (struct linkedNode *)arg;
    char isOpen = 1;
    int numRecv;
    time_t time;
    struct tm *tmInfo;
    char buff[16];

    while (isOpen) {
        numRecv = recv(node->data.cdata->sockFD, buffer, size, 0);
        if (numRecv < 1) {
            removeNode(&connectionList, node, 0, CONNECTION);
            pthread_exit((void *)1);
        } else if (numRecv < size) {
            fprintf(stderr, "Didn't get enough bytes for a packet.\n");
        } else {
            deserialisePacket(&packet, buffer);
            time = (time_t)packet.timestamp;
            tmInfo = localtime(&time);
            memset(buff, 0, ARRAY_SIZE(buff));
            strftime(buff, ARRAY_SIZE(buff), "%X", tmInfo);
            printf("Time: %s, Base: %u, MAC: %02X:%02X:%02X:%02X:%02X:%02X, RSSI: %d\n", buff, packet.baseNum,
                packet.mac[0], packet.mac[1], packet.mac[2], packet.mac[3], packet.mac[4],
                packet.mac[5], packet.rssi);
        }
    }

    pthread_exit(NULL);
}

void deserialisePacket(struct wisnPacket *packet, unsigned char *buffer) {
    unsigned char *marker = buffer;

    memcpy(&(packet->timestamp), marker, sizeof(packet->timestamp));
    packet->timestamp = be64toh(packet->timestamp);
    marker += sizeof(packet->timestamp);

    memcpy(&(packet->mac), marker, sizeof(packet->mac));
    marker += ARRAY_SIZE(packet->mac);

    memcpy(&(packet->rssi), marker, sizeof(packet->rssi));
    marker += sizeof(packet->rssi);

    memcpy(&(packet->baseNum), marker, sizeof(packet->baseNum));
    packet->baseNum = ntohs(packet->baseNum);
    marker += sizeof(packet->baseNum);
}
