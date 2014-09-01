#ifndef WISN
#define WISN

#include <stdio.h>
#include <stdlib.h>
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
#include <pcap.h>
#include <MQTTAsync.h>

#include "wisn_packet.h"
#include "radiotap.h"
#include "radiotap_iter.h"
#include "ieee80211.h"
#include "linked_list.h"
#include "khash.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

int main(int argc, char *argv[]);
//int runCommand(char *command, char **args);
int runCommand(const char *command, char *arg);
void cleanup(int ret);
pcap_t* initialisePcap(char *device);
void closePcap(pcap_t *pcapHandle);
void readPacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);
void changeChannel(char channel);
char getChannel(short frequency);
void* channelSwitcher(void *arg);
char compareMAC(const unsigned char *mac1, const unsigned char *mac2);
char checkInterface();
//int connectToServer(char *serverAddress);
//void *sendToServer(void *arg);
unsigned char *serialiseWisnPacket(struct wisnPacket *packet, unsigned int *size);
void signalList(struct linkedList *list);
unsigned int connectToBroker(MQTTAsync *mqttClient, char *address, unsigned int baseNum);
void connectionLost(void *context, char *cause);
void connectFailed(void *context, MQTTAsync_failureData *response);
void *sendToServer(void *arg);
void JSONisePacket(struct wisnPacket *packet, char *buffer, int size);
#endif
