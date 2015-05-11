#ifndef WISN
#define WISN

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <pthread.h>
#include <pcap.h>

#include "wisn_packet.h"
#include "wisn_version.h"
#include "radiotap.h"
#include "radiotap_iter.h"
#include "ieee80211.h"
#include "linked_list.h"
#include "mqtt.h"
#include "khash.h"

#define NUMCHANNELS 14
#define AVGTIMEOUT 300
#define AVGNUM 32

enum argState {ARG_NONE, ARG_PORT, ARG_BROKER, ARG_CHANNEL};

int main(int argc, char *argv[]);
//int runCommand(char *command, char **args);
int runCommand(const char *command, char *arg);
void cleanup(int ret);
void destroyStoredData(void);
pcap_t* initialisePcap(char *device);
void closePcap(pcap_t *pcapHandle);
void readPacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);
void changeChannel(char channel);
char getChannel(short frequency);
void* channelSwitcher(void *arg);
void calculateChannelTimeSlices(time_t *channelTime);
char compareMAC(const unsigned char *mac1, const unsigned char *mac2);
char checkInterface();
unsigned char *serialiseWisnPacket(struct wisnPacket *packet, unsigned int *size);
void signalList(struct linkedList *list);
unsigned int connectToBroker(char *address, unsigned int baseNum, int port);
void *sendToServer(void *arg);
void JSONisePacket(struct wisnPacket *packet, char *buffer, int size);
#endif
