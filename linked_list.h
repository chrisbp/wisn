#ifndef LINKED_LIST
#define LINKED_LIST

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include "wisn_packet.h"

//Struct for linked list
struct linkedList {
    struct linkedNode *head;
    struct linkedNode *tail;
    unsigned int size;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char doSignal;
};

//Struct for linked list nodes
struct linkedNode {
    struct linkedNode *next;
    struct linkedNode *prev;
    struct wisnPacket *data;
};

void initList(struct linkedList *linkedList);
void destroyList(struct linkedList *linkedList);
struct linkedNode *addPacketToTailList(struct linkedList *linkedList, struct wisnPacket *packet);
void addToTailList(struct linkedList *linkedList, struct linkedNode *node);
void removeFromHeadList(struct linkedList *linkedList, unsigned char haveLock);
void removeNode(struct linkedList *linkedList, struct linkedNode *node, unsigned char haveLock);
#endif
