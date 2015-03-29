#include "linked_list.h"

//Creates and initialises the given linked list
void initList(struct linkedList *linkedList) {
    linkedList->head = NULL;
    linkedList->tail = NULL;
    linkedList->size = 0;
    linkedList->doSignal = 0;
    if (pthread_mutex_init(&(linkedList->mutex), NULL)) {
        fprintf(stderr, "Error creating list mutex.\n");
    }
    if (pthread_cond_init(&(linkedList->cond), NULL)) {
        fprintf(stderr, "Error creating list condition variable.\n");
    }
}

//Deletes all contents and destroys the given linked list
void destroyList(struct linkedList *linkedList) {
    if (pthread_mutex_lock(&(linkedList->mutex))) {
        fprintf(stderr, "Error acquiring list mutex.\n");
    }

    //Remove all contents
    while (linkedList->size > 0) {
        removeFromHeadList(linkedList, LIST_HAVE_LOCK, LIST_DELETE_DATA);
    }

    if (pthread_mutex_unlock(&(linkedList->mutex))) {
        fprintf(stderr, "Error releasing list mutex.\n");
    }

    if (pthread_cond_destroy(&(linkedList->cond))) {
        fprintf(stderr, "Error destroying list condition variable.\n");
    }

    if (pthread_mutex_destroy(&(linkedList->mutex))) {
        fprintf(stderr, "Error destroying list mutex.\n");
    }
}

//Adds the given packet to the tail of the given list
struct linkedNode *addDataToTailList(struct linkedList *linkedList, void *data) {
    struct linkedNode *node = malloc(sizeof(*node));
    node->data = data;
    addToTailList(linkedList, node);
    return node;
}

//Adds the given node to the tail of the given list
void addToTailList(struct linkedList *linkedList, struct linkedNode *node) {
    if (pthread_mutex_lock(&(linkedList->mutex))) {
        fprintf(stderr, "Error acquiring list mutex.\n");
    } else {
        node->next = NULL;
        if (linkedList->tail == NULL) { //List is empty
            linkedList->head = node;
            linkedList->tail = node;
            node->prev = NULL;
        } else {    //List is not empty
            linkedList->tail->next = node;
            node->prev = linkedList->tail;
            linkedList->tail = node;
        }
        linkedList->size++;
        if (linkedList->doSignal) {
            if (pthread_cond_signal((&linkedList->cond))) {
                fprintf(stderr, "Error signalling condition variable.\n");
            }
        }
        if (pthread_mutex_unlock(&(linkedList->mutex))) {
            fprintf(stderr, "Error releasing list mutex.\n");
        }
    }
}

//Removes the node at the head of the given list
void removeFromHeadList(struct linkedList *linkedList, unsigned char haveLock,
        unsigned char deleteData) {

    if (linkedList->head != NULL) { //Check for empty list
        if (!haveLock) {
            if (pthread_mutex_lock(&(linkedList->mutex))) {
                fprintf(stderr, "Error acquiring list mutex.\n");
                return;
            }
        }

        if (deleteData) {
            free(linkedList->head->data);
        }

        if (linkedList->head->next == NULL) {   //Only one node in list
            free(linkedList->head);
            linkedList->head = NULL;
            linkedList->tail = NULL;
        } else {    //More than one node in list
            linkedList->head = linkedList->head->next;
            free(linkedList->head->prev);
            linkedList->head->prev = NULL;
        }

        linkedList->size--;

        if (!haveLock) {
            if (pthread_mutex_unlock(&(linkedList->mutex))) {
                fprintf(stderr, "Error releasing list mutex.\n");
            }
        }
    }
}

//Removes the given node from the given list
void removeNode(struct linkedList *linkedList, struct linkedNode *node,
        unsigned char haveLock, unsigned char deleteData) {

    if (node != NULL) { //Check node exists
        if (!haveLock) {
            if (pthread_mutex_lock(&(linkedList->mutex))) {
                fprintf(stderr, "Error acquiring list mutex.\n");
                return;
            }
        }

        if (deleteData) {
            free(node->data);
        }

        if (node->prev == NULL) {   //Node is at head of queue
            if (node->next == NULL) {   //Only one node in list
                linkedList->head = NULL;
                linkedList->tail = NULL;
            } else {    //More than one node in list
                linkedList->head = node->next;
                linkedList->head->prev = NULL;
            }
        } else if (node->next == NULL) {    //Node is at tail of queue
            linkedList->tail = node->prev;
            node->prev->next = NULL;
        } else {    //Node is somewhere in the middle
            node->next->prev = node->prev;
            node->prev->next = node->next;
        }

        free(node);

        linkedList->size--;

        if (!haveLock) {
            if (pthread_mutex_unlock(&(linkedList->mutex))) {
                fprintf(stderr, "Error releasing list mutex.\n");
            }
        }
    }
}
