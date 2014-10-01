#ifndef WISN_SERVER
#define WISN_SERVER

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_linalg.h>

#include "linked_list.h"
#include "wisn_packet.h"
#include "mqtt.h"
#include "khash.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define SERVERID "wisnServer"
#define SERVERTOPIC "wisn/#"

typedef enum {NODENUM, X, Y, TIME, MAC, RSSI, NONE} parseState;

int main(int argc, char *argv[]);
void cleanup(int ret);
void destroyStoredData(void);
unsigned int connectToBroker(char *address, int port);
void receivedMessage(struct mosquitto *conn, void *args, const struct mosquitto_message *message);
void convertMAC(unsigned long long mac, unsigned char *dest);
unsigned char parseHexChar(char *string);
void localiseDevice(struct linkedList *deviceList);
void removeOldData(struct linkedList *deviceList);
double getDistance(double rssi);
double calculateElementA(double xk, double xi);
double calculateElementB2D(double distancei, double distancek, double xi, double yi, double xk, double yk);
double calculateElementB3D(double distancei, double distancek, double xi, double yi, double zi, double xk, double yk, double zk);
void printMatrix(gsl_matrix *m);
void printVector(gsl_vector *v);

#endif
