#ifndef WISN_CONNECTION
#define WISN_CONNECTION

#include <sys/socket.h>
#include <pthread.h>

#define CONNECTION 1

struct wisnConnection {
    int sockFD;
    struct sockaddr_storage sockaddr;
    pthread_t thread;
};

#endif
