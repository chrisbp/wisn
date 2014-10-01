#include "wisn_server.h"

KHASH_MAP_INIT_INT64(devM, struct linkedList*)

const char *usage =  "Usage: wisn_server mqtt_address [mqtt_port]"; //Usage string

struct mosquitto *mosqConn;     //MQTT connection handle
int mqttPort;                   //MQTT broker port to connect on - default is 1883
volatile char isMQTTCreated;    //Flag for if MQTTClient has been initialised
volatile char isMQTTConnected;  //Flag for if connected to MQTT broker

khash_t(devM) *deviceMap;       //Hashmap for all device lists

int main(int argc, char *argv[]) {
    //Signal handler for kill/termination
    struct sigaction sa;

    if (argc > 1) {
        if (argc > 2) {
            mqttPort = strtoul(argv[2], NULL, 10);
            if (mqttPort < 1 || mqttPort > 65535) {
                fprintf(stderr, "Invalid port\n");
                fprintf(stderr, usage);
                exit(2);
            }
        } else {
            mqttPort = MQTTPORT;
        }
        
        deviceMap = kh_init(devM);
        if (connectToBroker(argv[1], mqttPort) != MOSQ_ERR_SUCCESS) {
            cleanup(3);
        }
    } else {
        fprintf(stderr, usage);
        exit(1);
    }

    //Setup signal handler
    sa.sa_handler = cleanup;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    struct linkedList *list;
    //struct linkedNode *node;
    //int packetNum = 0;
    //Try and calculate device positions every 3 seconds
    while (1) {
        sleep(3);
        for (khint_t it = kh_begin(deviceMap); it != kh_end(deviceMap); it++) {
            //packetNum = 0;
            if (kh_exist(deviceMap, it)) {
                printf("Device %llX at ", kh_key(deviceMap, it));
                list = kh_value(deviceMap, it);
                localiseDevice(list);
                /*node = list->head;
                while (node != NULL) {
                    printf("Packet #%d - ", packetNum);
                    printPacket(node->data);
                    node = node->next;
                    packetNum++;
                }*/
            }
        }
    }
}

/* Cleanup function called before exit.
 */
void cleanup(int ret) {
    if (isMQTTConnected) {
        isMQTTConnected = 0;
        mosquitto_disconnect(mosqConn);
        mosquitto_loop_stop(mosqConn, 0);
    }
    if (isMQTTCreated) {
        isMQTTCreated = 0;
        mosquitto_destroy(mosqConn);
        mosquitto_lib_cleanup();
    }
    destroyStoredData();
    exit(ret);
}

/* Clean up function for deleting and freeing all lists and map data.
 */
void destroyStoredData(void) {
    struct linkedList *list;
    for (khint_t it = kh_begin(deviceMap); it != kh_end(deviceMap); it++) {
        if (kh_exist(deviceMap, it)) {
            list = kh_value(deviceMap, it);
            destroyList(list);
            free(list);
        }
    }
    kh_destroy(devM, deviceMap);
}

/* Attempts to connect to the given MQTT broker.
 */
unsigned int connectToBroker(char *address, int port) {
    unsigned int res = 255;

    mosquitto_lib_init();
    mosqConn = mosquitto_new(SERVERID, 1, NULL);
    if (!mosqConn) {
        fprintf(stderr, "Error creating connection to MQTT broker.\n");
        return 1;
    }
    isMQTTCreated = 1;

    res = mosquitto_connect_async(mosqConn, address, port, KEEPALIVE);
    if (res != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect to MQTT broker: %s:%d.\n", address, port);
        return res;
    }
    isMQTTConnected = 1;

    mosquitto_reconnect_delay_set(mosqConn, RECONNECTDELAY, RECONNECTDELAY, 0);
    mosquitto_message_callback_set(mosqConn, receivedMessage);

    res = mosquitto_loop_start(mosqConn);
    if (res != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to start MQTT connection loop.\n");
        return res;
    }

    res = mosquitto_subscribe(mosqConn, NULL, SERVERTOPIC, 0);
    if (res != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to subscribe to MQTT topic.\n");
        return res;
    }

    return res;
}

/* Callback function for when a message is received from the MQTT broker.
 * This function parses received messages.
 */
void receivedMessage(struct mosquitto *conn, void *args, const struct mosquitto_message *message) {
    char *delims = "{}:,\"";
    struct wisnPacket *wisnData;
    char *it;
    parseState state;
    struct linkedList *list;
    unsigned long long mac = 0;
    int ret;

    char *messageCopy = (char *)malloc(sizeof(char) * message->payloadlen);
    strncpy(messageCopy, message->payload, message->payloadlen);
    wisnData = (struct wisnPacket *)malloc(sizeof(*wisnData));

    state = NONE;
    it = strtok(messageCopy, delims);
    while (it != NULL) {
        if (state == NONE) {
            if (strcmp(it, "node") == 0) {
                state = NODENUM;
            } else if (strcmp(it, "x") == 0) {
                state = X;
            } else if (strcmp(it, "y") == 0) {
                state = Y;
            } else if (strcmp(it, "time") == 0) {
                state = TIME;
            } else if (strcmp(it, "mac") == 0) {
                state = MAC;
            } else if (strcmp(it, "rssi") == 0) {
                state = RSSI;
            }
        } else {
            if (state == NODENUM) {
                wisnData->nodeNum = strtoul(it, NULL, 10);
            } else if (state == X) {
                wisnData->x = strtol(it, NULL, 10);
            } else if (state == Y) {
                wisnData->y = strtol(it, NULL, 10);
            } else if (state == TIME) {
                wisnData->timestamp = strtoll(it, NULL, 10);
            } else if (state == MAC) {
                mac = strtoull(it, NULL, 16);
                convertMAC(mac, wisnData->mac);
            } else if (state == RSSI) {
                wisnData->rssi = strtol(it, NULL, 10);
            }
            state = NONE;
        }
        it = strtok(NULL, delims);
    }

    printPacket(wisnData);
    
    //Store packet
    khint_t devIt = kh_get(devM, deviceMap, mac);
    if (devIt != kh_end(deviceMap)) {   //list already exists
        struct linkedNode *node;
        struct linkedNode *nextNode;
        list = kh_value(deviceMap, devIt);
        node = list->head;
        while (node != NULL) {  //Remove old packets from the same node
            if (node->data->nodeNum == wisnData->nodeNum) {
                nextNode = node->next;
                removeNode(list, node, 0);
                node = nextNode;
            } else {
                node = node->next;
            }
        }
        addPacketToTailList(list, wisnData);
    } else {    //no list exists yet
        list = (struct linkedList *)malloc(sizeof(struct linkedList));
        initList(list);
        addPacketToTailList(list, wisnData);
        devIt = kh_put(devM, deviceMap, mac, &ret);
        kh_value(deviceMap, devIt) = list;
    }

    free(messageCopy);
}

/* Converts the MAC address from a 64 bit integer to a char array
 */
void convertMAC(unsigned long long mac, unsigned char *dest) {
    dest[5] = mac & 0xFF;
    dest[4] = (mac >> 8) & 0xFF;
    dest[3] = (mac >> 16) & 0xFF;
    dest[2] = (mac >> 24) & 0xFF;
    dest[1] = (mac >> 32) & 0xFF;
    dest[0] = (mac >> 40) & 0xFF;
}

/* Reads two characters and returns their equivalent in hexadecimal.
 */
unsigned char parseHexChar(char *string) {
    unsigned char value = 0;
    for (unsigned char i = 0; i < 2; i++) {
        if (string[i] > 64 && string[i] < 71) { //A-Z
            value |= string[i] - 55;
        } else if (string[i] > 96 && string[i] < 103) { //a-z
            value |= string[i] - 87;
        } else if (string[i] > 47 && string[i] < 58) {  //0-9
            value |= string[i] - 48;
        }

        if (i == 0) {
            value = value << 4;
        }
    }
    return value;
}

/* Attempts to localise a device from the given list of received messages.
 */
void localiseDevice(struct linkedList *deviceList) {
    pthread_mutex_lock(&deviceList->mutex);

    removeOldData(deviceList);  //Remove data older than 60 seconds

    if (deviceList->size == 1) { //Can only assume a radius around one node
        printf("(%d, %d)\n", deviceList->head->data->x, deviceList->head->data->y);
    } else if (deviceList->size == 2) { //2 possible solutions, pick node with closer distance
        double d1 = getDistance((double)deviceList->head->data->rssi);
        double d2 = getDistance((double)deviceList->tail->data->rssi);
        if (d1 < d2) {
            printf("(%d, %d)\n", deviceList->head->data->x, deviceList->head->data->y);
        } else {
            printf("(%d, %d)\n", deviceList->tail->data->x, deviceList->tail->data->y);
        }
    } else if (deviceList->size > 2) {  //Enough data to use multilateration in 2D
        int numRows = deviceList->size - 1;
        int numCols = 2;    //2 for 2d, 3 for 3d
        struct linkedNode *anchorNode = deviceList->tail;
        struct linkedNode *node = deviceList->tail->prev;

        gsl_matrix *A = gsl_matrix_alloc(numRows, numCols);
        gsl_vector *x = gsl_vector_alloc(numCols);  //cols will always be smaller than rows
        gsl_vector *B = gsl_vector_alloc(numRows);  //rows will always be bigger than cols
        gsl_vector *tau = gsl_vector_alloc(numCols);//cols will always be smaller than rows
        gsl_vector *res = gsl_vector_alloc(numRows);//rows will always be bigger than cols

        //Create A and B from co-ordinates and distances
        for (int i = 0; i < numRows; i++) {
            gsl_matrix_set(A, i, 0, calculateElementA(anchorNode->data->x, node->data->x));
            gsl_matrix_set(A, i, 1, calculateElementA(anchorNode->data->y, node->data->y));

            gsl_vector_set(B, i, calculateElementB2D(getDistance((double)node->data->rssi),
                    getDistance((double)anchorNode->data->rssi), node->data->x, node->data->y,
                    anchorNode->data->x, anchorNode->data->y));

            node = node->prev;
        }

        gsl_linalg_QR_decomp(A, tau);
        gsl_linalg_QR_lssolve(A, tau, B, x, res);

        printf("(%.1f, %.1f)\n", gsl_vector_get(x, 0), gsl_vector_get(x, 1));

        gsl_matrix_free(A);
        gsl_vector_free(x);
        gsl_vector_free(B);
        gsl_vector_free(tau);
        gsl_vector_free(res);
    }

    pthread_mutex_unlock(&deviceList->mutex);
}

/* Iterates through data and removes anything older than 60 seconds than the newest data.
 */
void removeOldData(struct linkedList *deviceList) {
    if (deviceList->size > 1) {
        unsigned long long newest;
        struct linkedNode *node;
        struct linkedNode *prevNode;

        newest = deviceList->tail->data->timestamp;
        node = deviceList->tail->prev;
        while (node != NULL) {
            if (newest - node->data->timestamp > 60) {
                prevNode = node->prev;
                removeNode(deviceList, node, 1);
                node = prevNode;
            } else {
                node = node->prev;
            }
        }
    }
}

/* Calculates the distance between a device and node based on the RSSI.
 * distance = 0.0002 * (rssi ^ 2.9179)
 */
double getDistance(double rssi) {
    return 0.0002 * pow(rssi, 2.9179);
}

/* Calculates an element for matrix A.
 * element = 2 * (xk - xi)
 */
double calculateElementA(double xk, double xi) {
    return 2 * (xk - xi);
}

/* Calculates an element for vector B in 2D.
 * element = di^2 - dk^2 - xi^2 - yi^2 + xk^2 + yk^2
 */
double calculateElementB2D(double distancei, double distancek, double xi, double yi, double xk, double yk) {
    return (distancei * distancei) - (distancek * distancek) - (xi * xi) - (yi * yi) + (xk * xk) + (yk * yk);
}

/* Calculates an element for vector B in 3D.
 * element = di^2 - dk^2 - xi^2 - yi^2 - zi^2 + xk^2 + yk^2 + zk^2
 */
double calculateElementB3D(double distancei, double distancek, double xi, double yi, double zi, double xk, double yk, double zk) {
    return (distancei * distancei) - (distancek * distancek) - (xi * xi) - (yi * yi) - (zi * zi) + (xk * xk) + (yk * yk) + (zk * zk);
}

/* Prints a representation of the given matrix.
 */
void printMatrix(gsl_matrix *m) {
    for (int i = 0; i < m->size1; i++) {
        for (int j = 0; j < m->size2; j++) {
            printf("%f\t", gsl_matrix_get(m, i, j));
        }
        printf("\n");
    }
    printf("\n\n");
}

/* Prints a representation of the given vector.
 */
void printVector(gsl_vector *v) {
    for (int i = 0; i < v->size; i++) {
        printf("%f\t", gsl_vector_get(v, i));
    }
    printf("\n\n");
}
