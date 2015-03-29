#include "wisn_server.h"

KHASH_MAP_INIT_INT64(devM, struct linkedList *)
KHASH_MAP_INIT_INT(nodeM, struct wisnNode *)

struct linkedList calList;  //List of calibration points
struct linkedList dataList; //List of data queued for processing

const char *usage =  "Usage: wisn_server [OPTIONS]\n\n"
                     "-b address\tMQTT Broker address or URL.\tDefault is 127.0.0.1\n"
                     "-p port\t\tMQTT port to use.\t\tDefault is 1883\n"
                     "-v\t\twisn_server version\n";    //Usage string

struct mosquitto *mosqConn;         //MQTT connection handle
char *mqttBroker;                   //MQTT broker address
int mqttPort;                       //MQTT broker port to connect on - default is 1883
volatile char isMQTTCreated = 0;    //Flag for if MQTTClient has been initialised
volatile char isMQTTConnected = 0;  //Flag for if connected to MQTT broker
volatile char isDBInitialised = 0;  //Flag for if DB is initialised
volatile char isRunning = 0;        //Flag for if data processing loop should run
volatile char runUpdateNodes = 0;   //Flag for updating list of nodes
volatile char runUpdateCal = 0;     //Flag for updating calibration

khash_t(devM) *deviceMap;           //Hashmap for all device lists
khash_t(nodeM) *nodeMap;            //Hashmap for all node positions

mongoc_client_t *dbClient;          //Database client
mongoc_collection_t *nodesCol;      //Collection of node positions
mongoc_collection_t *positionsCol;  //Collection of device positions
mongoc_collection_t *calibrationCol;//Collection of calibration positions
bson_t *query;                      //Empty query to get everything in a collection

double pointsPerMeter;     //Calibration data for converting between meters and co-ordinates

int main(int argc, char *argv[]) {
    //Signal handler for kill/termination
    struct sigaction sa;

    mqttPort = MQTT_PORT;
    mqttBroker = MQTT_BROKER;

    if (argc > 1) {
        enum argState state = ARG_NONE;
        for (int i = 1; i < argc; i++) {
            if (state == ARG_NONE) {
                if (strcmp(argv[i], "-b") == 0) {
                    state = ARG_BROKER;
                } else if (strcmp(argv[i], "-p") == 0) {
                    if ((i + 1) < argc) {
                        state = ARG_PORT;
                    } else {
                        fprintf(stderr, "Invalid port\n");
                        fprintf(stderr, "\n%s\n", usage);
                        return 1;
                    }
                } else if (strcmp(argv[i], "-v") == 0) {
                    printf("\n%s\n", WISN_VERSION);
                    return 0;
                } else if (strcmp(argv[i], "--help") == 0) {
                    printf("\n%s\n", usage);
                    return 0;
                }
            } else if (state == ARG_BROKER) {
                mqttBroker = argv[i];
                state = ARG_NONE;
            } else if (state == ARG_PORT) {
                mqttPort = strtoul(argv[i], NULL, 10);
                if (mqttPort < 1 || mqttPort > 65535) {
                    fprintf(stderr, "Invalid port\n");
                    fprintf(stderr, "\n%s\n", usage);
                    return 1;
                }
                state = ARG_NONE;
            }
        }
    }

    initList(&calList);
    initList(&dataList);
    dataList.doSignal = 1;
    deviceMap = kh_init(devM);
    nodeMap = kh_init(nodeM);
    if (connectToBroker(mqttBroker, mqttPort) != MOSQ_ERR_SUCCESS) {
        cleanup(2);
    }
    initialiseDBConnection(DB_URL, DB_NAME);

    //Setup signal handler
    sa.sa_handler = cleanup;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    updateCalibration();
    updateNodes();

    isRunning = 1;

    while (isRunning) {
        struct wisnPacket *packet;
        struct linkedList *list;

        while (pthread_mutex_lock(&(dataList.mutex))) { //Lock mutex to access data queue
            fprintf(stderr, "Error acquiring list mutex.\n");
        }

        while (dataList.head == NULL && isRunning) {    //If queue is empty, unlock mutex and wait
            if (pthread_cond_wait(&(dataList.cond), &(dataList.mutex))) {
                fprintf(stderr, "Error waiting for condition variable signal\n");
            }
        }

        if (!isRunning) {
            break;
        }

        if (runUpdateNodes) {   //Update list of nodes
            updateNodes();
            runUpdateNodes = 0;
        }
        if (runUpdateCal) {     //Update calibration data
            updateCalibration();
            runUpdateCal = 0;
        }

        packet = dataList.head->data; //Get packet to process
        removeFromHeadList(&dataList, LIST_HAVE_LOCK, LIST_KEEP_DATA);   //Remove from data queue

        if (pthread_mutex_unlock(&(dataList.mutex))) {    //Unlock mutex so new data can be added
            fprintf(stderr, "Error releasing list mutex.\n");
        }

        printPacket(packet);
        list = storeWisnPacket(packet); //Store the packet and get the list of packets for this device
        localiseDevice(list);   //Perform localisation for device
    }
}

/* Cleanup function called before exit.
 */
void cleanup(int ret) {
    isRunning = 0;
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
    if (isDBInitialised) {
        cleanupDB();
    }
    destroyList(&calList);
    destroyList(&dataList);
    destroyStoredData();
    exit(ret);
}

/* Clean up function for deleting and freeing all lists and map data.
 */
void destroyStoredData(void) {
    struct linkedList *list;
    struct wisnNode *node;

    for (khint64_t it = kh_begin(deviceMap); it != kh_end(deviceMap); it++) {
        if (kh_exist(deviceMap, it)) {
            list = kh_value(deviceMap, it);
            destroyList(list);
            free(list);
        }
    }
    kh_destroy(devM, deviceMap);

    for (khint_t it = kh_begin(nodeMap); it != kh_end(nodeMap); it++) {
        if (kh_exist(nodeMap, it)) {
            node = kh_value(nodeMap, it);
            free(node);
        }
    }
    kh_destroy(nodeM, nodeMap);
}

/* Attempts to connect to the given MQTT broker.
 */
unsigned int connectToBroker(char *address, int port) {
    unsigned int res = 255;

    mosquitto_lib_init();
    mosqConn = mosquitto_new(SERVER_ID, 1, NULL);
    if (!mosqConn) {
        fprintf(stderr, "Error creating connection to MQTT broker.\n");
        return 1;
    }
    isMQTTCreated = 1;

    res = mosquitto_connect_async(mosqConn, address, port, KEEPALIVE);
    if (res != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to connect to MQTT broker: %s:%d.\n", address,
                port);
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

    res = mosquitto_subscribe(mosqConn, NULL, SERVER_TOPIC, 0);
    if (res != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to subscribe to MQTT topic.\n");
        return res;
    }

    return res;
}

/* Callback function for when a message is received from the MQTT broker.
 * This function parses received messages.
 */
void receivedMessage(struct mosquitto *conn, void *args,
                     const struct mosquitto_message *message) {

    if (strcmp(EVENTS_TOPIC, message->topic) == 0) {
        if (strcmp(EVENT_NODE, message->payload) == 0) {
            runUpdateNodes = 1;
        } else if (strcmp(EVENT_CAL, message->payload) == 0) {
            runUpdateCal = 1;
        }
    } else if (strcmp(POSITIONS_TOPIC, message->topic) == 0) {
        //Ignore messages the server sends
    } else {
        receivedDeviceMessage(message);
    }
}

/* Read the received device message, store the data and localise device.
 */
void receivedDeviceMessage(const struct mosquitto_message *message) {
    struct wisnPacket *wisnData;

    wisnData = readJson(JSON_DEVICE, message->payload);
    addDataToTailList(&dataList, wisnData);
}

/* Converts the MAC address from a 64 bit integer to a char array.
 */
void ui64ToChars(unsigned long long mac, unsigned char *dest) {
    dest[5] = mac & 0xFF;
    dest[4] = (mac >> 8) & 0xFF;
    dest[3] = (mac >> 16) & 0xFF;
    dest[2] = (mac >> 24) & 0xFF;
    dest[1] = (mac >> 32) & 0xFF;
    dest[0] = (mac >> 40) & 0xFF;
}

/* Converts the MAC address from a char array to a 64 bit integer.
 */
unsigned long long charsToui64(unsigned char *mac) {
    unsigned long long macAddr = 0;

    for (int i = 0; i < ARRAY_SIZE(mac); i++) {
        macAddr += mac[i];
        macAddr = macAddr << 8;
    }

    return macAddr;
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
    double xPos;
    double yPos;
    char buffer[128];
    struct wisnNode *node1 = NULL;
    struct wisnNode *node2 = NULL;
    struct wisnPacket *packet1 = NULL;
    struct wisnPacket *packet2 = NULL;
    char havePosition = 0;
    int numNodes = 0;

    pthread_mutex_lock(&deviceList->mutex);

    removeOldData(deviceList);  //Remove data older than 60 seconds

    for (struct linkedNode *nodeIt = deviceList->head; nodeIt != NULL;
         nodeIt = nodeIt->next) {

        packet1 = nodeIt->data;
        node1 = getNode(packet1->nodeNum);
        if (node1 != NULL) {
            numNodes++;
        }
    }

    if (deviceList->size == 1 && numNodes > 0) { //Can only assume a radius around one node
        packet1 = deviceList->head->data;
        node1 = getNode(packet1->nodeNum);
        if (node1 != NULL) {
            xPos = node1->x;
            yPos = node1->y;
            havePosition = 1;
        }
    } else if (deviceList->size == 2 && numNodes > 0) { //2 possible solutions, pick node with closer distance
        packet1 = deviceList->head->data;
        node1 = getNode(packet1->nodeNum);
        packet2 = deviceList->tail->data;
        node2 = getNode(packet2->nodeNum);

        if (node1 != NULL && node2 != NULL) {
            double bestRSSI = min(packet1->rssi, packet2->rssi);
            if (bestRSSI == packet1->rssi) {
                xPos = node1->x;
                yPos = node1->y;
                havePosition = 2;
            } else {
                packet1 = packet2;
                xPos = node2->x;
                yPos = node2->y;
                havePosition = 3;
            }
        } else if (node1 != NULL) {
                xPos = node1->x;
                yPos = node1->y;
                havePosition = 4;
        } else if (node2 != NULL) {
            packet1 = packet2;
            xPos = node2->x;
            yPos = node2->y;
            havePosition = 5;
        }
    } else if (deviceList->size > 2 && numNodes > 2) {  //Enough data to use multilateration in 2D
        double anchorDistance;
        struct linkedNode *startListNode = NULL;
        int numRows = numNodes - 1;
        int numCols = 2;    //2 for 2d, 3 for 3d

        gsl_matrix *A = gsl_matrix_alloc(numRows, numCols);
        gsl_vector *x = gsl_vector_alloc(numCols);  //cols will always be smaller than rows
        gsl_vector *B = gsl_vector_alloc(numRows);  //rows will always be bigger than cols
        gsl_vector *tau = gsl_vector_alloc(numCols);//cols will always be smaller than rows
        gsl_vector *res = gsl_vector_alloc(numRows);//rows will always be bigger than cols

        //Find the first node and use as anchor node
        for (struct linkedNode *nodeIt = deviceList->head; nodeIt != NULL;
             nodeIt = nodeIt->next) {

            packet1 = nodeIt->data;
            node1 = getNode(packet1->nodeNum);
            if (node1 != NULL) {
                startListNode = nodeIt->next;
                break;
            }
        }

        anchorDistance = getDistance(packet1->rssi);

        //Create A and B from co-ordinates and distances
        int i = 0;
        for (struct linkedNode *nodeIt = startListNode; nodeIt != NULL;
             nodeIt = nodeIt->next) {

            packet2 = nodeIt->data;
            node2 = getNode(packet2->nodeNum);
            if (node2 != NULL) {
                gsl_matrix_set(A, i, 0, calculateElementA(node1->x, node2->x));
                gsl_matrix_set(A, i, 1, calculateElementA(node1->y, node2->y));

                gsl_vector_set(B, i, calculateElementB2D(getDistance(packet2->rssi),
                               anchorDistance, node2->x, node2->y,
                               node1->x, node1->y));
                i++;
            }
        }

        gsl_linalg_QR_decomp(A, tau);
        gsl_linalg_QR_lssolve(A, tau, B, x, res);

        xPos = gsl_vector_get(x, 0);
        yPos = gsl_vector_get(x, 1);
        if (xPos >= 0.0 && xPos <= 255.0 && yPos >= 0.0 && yPos <= 255.0) {
            havePosition = 6;
        }

        gsl_matrix_free(A);
        gsl_vector_free(x);
        gsl_vector_free(B);
        gsl_vector_free(tau);
        gsl_vector_free(res);
    }

    pthread_mutex_unlock(&deviceList->mutex);

    //If there is a position inside the bounds, send it
    if (havePosition) {
        printf("Type %d - %02X:%02X:%02X:%02X:%02X:%02X at (%.1f, %.1f)\n",
               havePosition, packet1->mac[0], packet1->mac[1], packet1->mac[2],
               packet1->mac[3], packet1->mac[4], packet1->mac[5], xPos, yPos);
        updatePositionDB(packet1, xPos, yPos);
        JSONisePosition(packet1, xPos, yPos, buffer, ARRAY_SIZE(buffer));
        mosquitto_publish(mosqConn, NULL, POSITIONS_TOPIC, strlen(buffer),
                          buffer, 0, 0);
    }
}

/* Iterates through data and removes anything older than 60 seconds than the
 * newest data.
 */
void removeOldData(struct linkedList *deviceList) {
    if (deviceList->size > 1) {
        struct linkedNode *node;
        struct linkedNode *nextNode;
        struct wisnPacket *packet;
        time_t now = time(NULL);

        node = deviceList->head;    //Head will have oldest data
        while (node != NULL) {
            packet = node->data;
            if (now - packet->timestamp > TIMEOUT) {
                nextNode = node->next;
                removeNode(deviceList, node, LIST_HAVE_LOCK, LIST_DELETE_DATA);
                node = nextNode;
            } else {    //Stop if current packet hasn't timed out, because all other packets are newer
                break;
            }
        }
    }
}

/* Calculates the distance between a device and node based on the RSSI.
 * Returns a distance scaled by calibration value.
 * distance = 0.0002 * (rssi ^ 2.9179) * scaleFactor
 */
double getDistance(double rssi) {
    return 0.0002 * pow(rssi, 2.9179) * pointsPerMeter;
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
double calculateElementB2D(double distancei, double distancek,
                           double xi, double yi, double xk, double yk) {

    return (distancei * distancei) - (distancek * distancek) - (xi * xi) -
           (yi * yi) + (xk * xk) + (yk * yk);
}

/* Calculates an element for vector B in 3D.
 * element = di^2 - dk^2 - xi^2 - yi^2 - zi^2 + xk^2 + yk^2 + zk^2
 */
double calculateElementB3D(double distancei, double distancek,
                           double xi, double yi, double zi,
                           double xk, double yk, double zk) {

    return (distancei * distancei) - (distancek * distancek) - (xi * xi) -
           (yi * yi) - (zi * zi) + (xk * xk) + (yk * yk) + (zk * zk);
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

/* Initialises and connects to the DB.
 */
void initialiseDBConnection(char *url, char *dbName) {
    mongoc_init();
    dbClient = mongoc_client_new(url);
    nodesCol = mongoc_client_get_collection(dbClient, dbName, DB_COL_NODES);
    positionsCol = mongoc_client_get_collection(dbClient, dbName,DB_COL_POSITIONS);
    calibrationCol = mongoc_client_get_collection(dbClient, dbName, DB_COL_CALIBRATION);
    query = bson_new();
    isDBInitialised = 1;
}

/* Disconnects and destroys all DB connections.
 */
void cleanupDB(void) {
    bson_destroy(query);
    mongoc_collection_destroy(nodesCol);
    mongoc_collection_destroy(positionsCol);
    mongoc_collection_destroy(calibrationCol);
    mongoc_client_destroy(dbClient);
    mongoc_cleanup();
}

/* Updates the list of all nodes.
 */
void updateNodes(void) {
    const bson_t *doc;
    char *data;
    struct wisnNode *node;
    int ret;

    mongoc_cursor_t *cursor = mongoc_collection_find(nodesCol,
            MONGOC_QUERY_NONE, 0, 0, 0, query, NULL, NULL);

    while (mongoc_cursor_next(cursor, &doc)) {
        data = bson_as_json(doc, NULL);
        node = readJson(JSON_NODE, data);

        khint_t nodeIt = kh_get(nodeM, nodeMap, node->nodeNum);
        if (nodeIt != kh_end(nodeMap)) {    //There is already data for this node
            struct wisnNode *oldNode = kh_value(nodeMap, nodeIt);
            free(oldNode);
        } else {    //No entry for this node, create new key
            nodeIt = kh_put(nodeM, nodeMap, node->nodeNum, &ret);
        }
        kh_value(nodeMap, nodeIt) = node;   //Update node data

        bson_free(data);
    }

    mongoc_cursor_destroy(cursor);
}

/* Reads the given JSON string and converts it and returns the specified
 * struct type.
 */
void *readJson(enum jsonType type, const char *json) {
    char *dataCopy;
    char *it;
    enum parseState state;
    struct wisnPacket *wisnPacket = NULL;
    struct wisnNode *wisnNode = NULL;
    struct wisnCalibration *wisnCal = NULL;
    unsigned long long mac = 0;

    //Check which struct needs to be initialised
    if (type == JSON_DEVICE) {
        wisnPacket = malloc(sizeof(*wisnPacket));
    } else if (type == JSON_NODE) {
        wisnNode = malloc(sizeof(*wisnNode));
    } else if (type == JSON_CAL) {
        wisnCal = malloc(sizeof(*wisnCal));
    } else {
        return NULL;
    }

    //Copy string since strtok is destructive
    dataCopy = calloc(strlen(json), sizeof(char));
    strncpy(dataCopy, json, strlen(json));
    state = PARSE_NONE;

    it = strtok(dataCopy, JSON_DELIMS);
    while (it != NULL) {
        if (strcmp(it, "") != 0 && strcmp(it, " ") != 0) {
            if (state == PARSE_NONE) {
                if (type == JSON_DEVICE && strcmp(it, "node") == 0) {
                    state = PARSE_NODENUM;
                } else if (type == JSON_DEVICE && strcmp(it, "time") == 0) {
                    state = PARSE_TIME;
                } else if (type == JSON_DEVICE && strcmp(it, "mac") == 0) {
                    state = PARSE_MAC;
                } else if (type == JSON_DEVICE && strcmp(it, "rssi") == 0) {
                    state = PARSE_RSSI;
                } else if ((type == JSON_NODE || type == JSON_CAL) && strcmp(it, "name") == 0) {
                    state = PARSE_NAME;
                } else if ((type == JSON_NODE || type == JSON_CAL) && strcmp(it, "x") == 0) {
                    state = PARSE_X;
                } else if ((type == JSON_NODE || type == JSON_CAL) && strcmp(it, "y") == 0) {
                    state = PARSE_Y;
                }
            } else {
                if (state == PARSE_NODENUM) {
                    wisnPacket->nodeNum = strtoul(it, NULL, 10);
                } else if (state == PARSE_TIME) {
                    wisnPacket->timestamp = strtoll(it, NULL, 10);
                } else if (state == PARSE_MAC) {
                    mac = strtoull(it, NULL, 16);
                    ui64ToChars(mac, wisnPacket->mac);
                } else if (state == PARSE_RSSI) {
                    wisnPacket->rssi = strtol(it, NULL, 10);
                } else if (state == PARSE_NAME) {
                    if (type == JSON_NODE) {    //Get node number
                        int marker = strcspn(it, "0123456789");
                        wisnNode->nodeNum = strtoul(it + marker, NULL, 10);
                    } else if (type == JSON_CAL) {  //Calibration name has distance info
                        char *marker = strstr(it, "start"); //Check if this is a "start" type
                        if (marker == it) { //If it is, find the distance part
                            int marker2 = strcspn(it, "0123456789");
                            wisnCal->calibration = strtod(it + marker2, NULL);
                            wisnCal->type = CAL_START;
                            wisnCal->name = malloc(sizeof(char) * (strlen(it) + 1));
                            strcpy(wisnCal->name, it);
                        } else {
                            marker = strstr(it, "end"); //Check if this is an "end" type
                            if (marker == it) { //If it is, find the distance part
                                int marker2 = strcspn(it, "0123456789");
                                wisnCal->calibration = strtod(it + marker2, NULL);
                                wisnCal->type = CAL_END;
                                wisnCal->name = malloc(sizeof(char) * (strlen(it) + 1));
                                strcpy(wisnCal->name, it);
                            }
                        }
                    }
                } else if (state == PARSE_X) {
                    if (type == JSON_NODE) {
                        wisnNode->x = strtod(it, NULL);
                    } else if (type == JSON_CAL) {
                        wisnCal->x = strtod(it, NULL);
                    }
                } else if (state == PARSE_Y ) {
                    if (type == JSON_NODE) {
                        wisnNode->y = strtod(it, NULL);
                    } else if (type == JSON_CAL) {
                        wisnCal->y = strtod(it, NULL);
                    }
                }
                state = PARSE_NONE;
            }
        }
        it = strtok(NULL, JSON_DELIMS);
    }

    free(dataCopy);

    //Return the correct struct type
    if (type == JSON_DEVICE) {
        return wisnPacket;
    } else if (type == JSON_NODE) {
        return wisnNode;
    } else if (type == JSON_CAL) {
        return wisnCal;
    } else {
        return NULL;
    }
}

/* Updates the current calibration value.
 */
void updateCalibration(void) {
    const bson_t *doc;
    char *data;
    struct wisnCalibration *cal = NULL;

    mongoc_cursor_t *cursor = mongoc_collection_find(calibrationCol,
            MONGOC_QUERY_NONE, 0, 0, 0, query, NULL, NULL);

    while (mongoc_cursor_next(cursor, &doc)) {
        data = bson_as_json(doc, NULL);
        cal = readJson(JSON_CAL, data);
        printf("Point %s at %f,%f of distance %f\n", cal->name, cal->x, cal->y,
               cal->calibration);
        addDataToTailList(&calList, cal);
        bson_free(data);
    }

    mongoc_cursor_destroy(cursor);

    while (pthread_mutex_lock(&(calList.mutex))) {
        fprintf(stderr, "Error acquiring list mutex.\n");
    }

    char foundPair = 0;
    struct linkedNode *node = calList.head;
    struct wisnCalibration *calIt = NULL;
    while (!foundPair && node != NULL) {
        cal = node->data;
        for (struct linkedNode *nodeIt = node->next; nodeIt != NULL;
             nodeIt = nodeIt->next) {

            calIt = nodeIt->data;
            if (cal->calibration == calIt->calibration &&
                cal->type != calIt->type) {

                foundPair = 1;
                break;
            }
        }
        node = node->next;
    }

    if (node == NULL) {
        pointsPerMeter = 1.0;
    } else {
        double xDist = max(cal->x, calIt->x) - min(cal->x, calIt->x);
        double yDist = max(cal->y, calIt->y) - min(cal->y, calIt->y);
        pointsPerMeter = max(xDist, yDist) / cal->calibration;
    }

    printf("Calibration factor is %f\n", pointsPerMeter);

    if (pthread_mutex_unlock(&(calList.mutex))) {
        fprintf(stderr, "Error releasing list mutex.\n");
    }
}

/* Returns the larger value of a and b.
 */
double max(double a, double b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

/* Returns the smaller value of a and b.
 */
double min(double a, double b) {
    if (a < b) {
        return a;
    } else {
        return b;
    }
}

/* Searches the hashmap of nodes for the node with the given node number.
 * Returns the wisnNode struct if the node is found; otherwise null.
 */
struct wisnNode *getNode(unsigned short nodeNum) {
    khint_t nodeIt = kh_get(nodeM, nodeMap, nodeNum);
    if (nodeIt != kh_end(nodeMap)) {
        return kh_value(nodeMap, nodeIt);
    } else {
        return NULL;
    }
}

/* Stores the given packet in the hashmap of devices.
 * Returns the list of all packets for the device the packet is from.
 */
struct linkedList *storeWisnPacket(struct wisnPacket *packet) {
    struct linkedList *list;
    int ret;
    unsigned long long mac;

    mac = charsToui64(packet->mac);
    khint64_t devIt = kh_get(devM, deviceMap, mac);
    if (devIt != kh_end(deviceMap)) {   //list already exists
        struct linkedNode *node;
        struct linkedNode *nextNode;
        list = kh_value(deviceMap, devIt);
        node = list->head;
        while (node != NULL) {  //Only want latest packet from each node so remove old ones
            struct wisnPacket *oldPacket = node->data;
            if (oldPacket->nodeNum == packet->nodeNum) {
                nextNode = node->next;
                removeNode(list, node, LIST_NO_LOCK, LIST_DELETE_DATA);
                node = nextNode;
            } else {
                node = node->next;
            }
        }
    } else {    //no list exists yet
        list = malloc(sizeof(struct linkedList));
        initList(list);
        devIt = kh_put(devM, deviceMap, mac, &ret);
        kh_value(deviceMap, devIt) = list;
    }
    addDataToTailList(list, packet);
    return list;
}

/* Updates the position of the given device in the database.
 */
void updatePositionDB(struct wisnPacket *packet, double x, double y) {
    bson_error_t error;
    char macString[13]; //MAC address is 2x6 hex chars + null terminator

    snprintf(macString, ARRAY_SIZE(macString), "%02X%02X%02X%02X%02X%02X",
             packet->mac[0], packet->mac[1], packet->mac[2], packet->mac[3],
             packet->mac[4], packet->mac[5]);

    bson_t *query = BCON_NEW("mac", BCON_UTF8(macString));
    bson_t *data = BCON_NEW("$set", "{", "x", BCON_DOUBLE(x), "y", BCON_DOUBLE(y), "}");
    if (!mongoc_collection_update(positionsCol, MONGOC_UPDATE_UPSERT, query, data, NULL, &error)) {
        printf("Error inserting position: %s\n", error.message);
    }

    bson_destroy(data);
    bson_destroy(query);
}

/* Turns the given packet into a JSON structure.
 */
void JSONisePosition(struct wisnPacket *packet, double xPos, double yPos,
                     char *buffer, int size) {

    memset(buffer, 0, size);

    snprintf(buffer, size,
            "{\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"x\":%f,\"y\":%f}",
            packet->mac[0], packet->mac[1], packet->mac[2], packet->mac[3],
            packet->mac[4], packet->mac[5], xPos, yPos);
}
