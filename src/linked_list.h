#ifndef LINKED_LIST
#define LINKED_LIST

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define LIST_HAVE_LOCK 1
#define LIST_NO_LOCK 0
#define LIST_DELETE_DATA 1
#define LIST_KEEP_DATA 0

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
    void *data;
};

void initList(struct linkedList *linkedList);
void destroyList(struct linkedList *linkedList, unsigned char deleteData);
struct linkedNode *addDataToTailList(struct linkedList *linkedList, void *data);
void addToTailList(struct linkedList *linkedList, struct linkedNode *node);
void removeFromHeadList(struct linkedList *linkedList, unsigned char haveLock, unsigned char deleteData);
void removeNode(struct linkedList *linkedList, struct linkedNode *node, unsigned char haveLock, unsigned char deleteData);

#endif
