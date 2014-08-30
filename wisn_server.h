#ifndef WISN_SERVER
#define WISN_SERVER

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <unistd.h>
#include <endian.h>

#include <ifaddrs.h>

#include <pthread.h>

#include "linked_list.h"
#include "wisn_packet.h"
#include "wisn_connection.h"

#define MAX_CONNS 20

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

int main(int argc, char *argv[]);
int createListenSocket(char *port);
void cleanup();
void *readPackets(void *arg);
void deserialisePacket(struct wisnPacket *packet, unsigned char *buffer);

#endif
