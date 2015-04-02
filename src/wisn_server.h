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

#include <mongoc.h>

#include "linked_list.h"
#include "wisn_packet.h"
#include "wisn_node.h"
#include "wisn_calibration.h"
#include "wisn_version.h"
#include "wisn_user.h"
#include "mqtt.h"
#include "khash.h"

#define JSON_DELIMS "{}:,\""
#define SERVER_ID "wisnServer"
#define SERVER_TOPIC "wisn/#"
#define EVENTS_TOPIC "wisn/events"
#define POSITIONS_TOPIC "wisn/positions"
#define EVENT_NODE "nodeUpdate"
#define EVENT_CAL "calibrationUpdate"
#define EVENT_USER "userUpdate"
#define TIMEOUT 300
#define DB_URL "mongodb://localhost:27020/"
#define DB_NAME "wisn"
#define DB_COL_NODES "nodes"
#define DB_COL_POSITIONS "positions"
#define DB_COL_CALIBRATION "calibration"
#define DB_COL_REGISTERED "names"

enum argState {ARG_NONE, ARG_BROKER, ARG_PORT};
enum parseState {PARSE_NODENUM, PARSE_TIME, PARSE_MAC, PARSE_RSSI, PARSE_NAME,
                 PARSE_X, PARSE_Y, PARSE_NONE};
enum jsonType {JSON_DEVICE, JSON_NODE, JSON_CAL, JSON_USER};

// static const char * const defaultDBURL = "mongodb://localhost:27020/";
// static const char * const dbName = "wisn";
// static const char * const nodesColName = "nodes";
// static const char * const positionsColName = "positions";
// static const char * const calibrationColName = "calibration";
// static const char * const jsonDelims = "{}:,\"";

int main(int argc, char *argv[]);
void stopRunning(int ret);
void cleanup(int ret);
void destroyStoredData(void);
unsigned int connectToBroker(char *address, int port);
void receivedMessage(struct mosquitto *conn, void *args, const struct mosquitto_message *message);
void receivedDeviceMessage(const struct mosquitto_message *message);
void ui64ToChars(unsigned long long mac, unsigned char *dest);
unsigned long long charsToui64(unsigned char *mac);
unsigned char parseHexChar(char *string);
void stringToMAC(char *string, unsigned char *mac);
void localiseDevice(struct linkedList *deviceList);
void removeOldData(struct linkedList *deviceList);
double getDistance(double rssi);
double calculateElementA(double xk, double xi);
double calculateElementB2D(double distancei, double distancek, double xi, double yi, double xk, double yk);
double calculateElementB3D(double distancei, double distancek, double xi, double yi, double zi, double xk, double yk, double zk);
void printMatrix(gsl_matrix *m);
void printVector(gsl_vector *v);
void initialiseDBConnection(char *url, char *dbName);
void cleanupDB(void);
void updateNodes(void);
void *readJson(enum jsonType type, const char *json);
char *findCharStart(char *string);
void updateCalibration(void);
double max(double a, double b);
double min(double a, double b);
struct wisnNode *getNode(unsigned short nodeNum);
struct linkedList *storeWisnPacket(struct wisnPacket *packet);
void updatePositionDB(struct wisnPacket *packet, double x, double y);
void JSONisePosition(struct wisnPacket *packet, double xPos, double yPos, char *buffer, int size);
void updateRegisteredUsers(void);

#endif
