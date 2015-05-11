#include "wisn.h"

KHASH_MAP_INIT_INT64(pckM, struct linkedList *)
KHASH_MAP_INIT_INT64(lastM, time_t)

const char *ifplugdCommand = "ifplugd -i %s -k";                //Command to stop ifplugd
const char *wpaSupCommand = "killall wpa_supplicant";           //Command to stop wpa_supplicant
const char *ifconfigDownCommand = "ifconfig %s down";           //Command to bring down a network interface
const char *ifconfigUpCommand = "ifconfig %s up";               //Command to bring up a network interface
const char *iwconfigChannelCommand = "iwconfig %s channel %d";  //Command to change wifi channel

unsigned int nodeNum;   //Number of this base node
char *wifiInterface;    //Name of wifi interface
const char *usage = "Usage: wisn node_num wifi_interface [OPTIONS]\n\n"
                    "-b broker\tMQTT broker to use\n"
                    "-p port\t\tMQTT port to use\n"
                    "-v\t\twisn version\n";         //Usage String

pcap_t *pcapHandle;             //Pcap handle for the wifi interface to listen on
volatile char isPcapOpen;       //Flag to indicate whether pcap handle is open or closed

pthread_t channelThread;        //Thread for channel switcher
pthread_t commsThread;          //Thread for communicating with MQTT broker
volatile char isChannelThreadRunning;   //Flag for if channels switcher is running

struct linkedList packetList;   //Linked list to queue up packets received over wifi
khash_t(pckM) *packetMap;       //Hashmap for storing all averaged rssi readings
khash_t(lastM) *lastSentMap;    //Hashmap for storing timestamp of the last packet sent to server

unsigned int packetTotals[NUMCHANNELS];
unsigned int channelIndex;
volatile char isChannelReady;
int singleChannel;              //The channel to listen on if set

struct mosquitto *mosqConn;     //MQTT connection handle
char *mqttBroker;               //MQTT broker address
int mqttPort;                   //MQTT broker port to connect on - default is 1883
volatile char isMQTTCreated;    //Flag for if MQTTClient has been initialised
volatile char isMQTTConnected;  //Flag for if connected to MQTT broker
char MQTTTopic[16];             //Topic for publishing messages

int main(int argc, char *argv[]) {
    unsigned char size;
    //Signal handler for kill/termination
    struct sigaction sa;
    pthread_attr_t attr;

    mqttPort = MQTT_PORT;
    mqttBroker = MQTT_BROKER;

    if (argc > 1) {
        if (strcmp(argv[1], "--help") == 0) {
            printf("\n%s\n", usage);
            return 0;
        } else if (strcmp(argv[1], "-v") == 0) {
            printf("\n%s\n", WISN_VERSION);
            return 0;
        }
    }
    if (argc > 2) {
        nodeNum = strtoul(argv[1], NULL, 10);
        if (nodeNum < 1) {
            fprintf(stderr, "Invalid node number.\n");
            return 2;
        }
        memset(MQTTTopic, 0, ARRAY_SIZE(MQTTTopic));
        snprintf(MQTTTopic, ARRAY_SIZE(MQTTTopic), "wisn/wisn%03u", nodeNum);

        if (checkInterface(argv[2]) == 0) {
            fprintf(stderr, "No network interface with name %s found.\n", argv[2]);
            return 3;
        } else {
            size = sizeof(char) * (strlen(argv[2]) + 1);
            wifiInterface = calloc(1, size);
            memcpy(wifiInterface, argv[2], size);
        }

        mqttPort = MQTT_PORT;

        if (argc > 3) {
            enum argState state = ARG_NONE;
            for (int i = 3; i < argc; i++) {
                if (state == ARG_NONE) {
                    if (strcmp(argv[i], "-p") == 0) {
                        state = ARG_PORT;
                    } else if (strcmp(argv[i], "-b") == 0) {
                        state = ARG_BROKER;
                    } else if (strcmp(argv[i], "-c") == 0) {
                        state = ARG_CHANNEL;
                    }
                } else if (state == ARG_PORT) {
                    mqttPort = strtoul(argv[i], NULL, 10);
                    if (mqttPort < 1 || mqttPort > 65535) {
                        fprintf(stderr, "Invalid port\n");
                        fprintf(stderr, "%s", usage);
                        return 4;
                    }
                    state = ARG_NONE;
                } else if (state == ARG_BROKER) {
                    mqttBroker = argv[i];
                    state = ARG_NONE;
                } else if (state == ARG_CHANNEL) {
                    singleChannel = strtol(argv[i], NULL, 10);
                    if (singleChannel < 1 || singleChannel > 14) {
                        fprintf(stderr, "Invalid channel\n");
                        fprintf(stderr, "%s", usage);
                        return 5;
                    }
                    state = ARG_NONE;
                }
            }
        }

    } else {
        printf("\n%s\n", usage);
        return 1;
    }

    //Setup signal handler
    sa.sa_handler = cleanup;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    isPcapOpen = 0;
    isChannelThreadRunning = 0;
    initList(&packetList);
    memset(packetTotals, 0, ARRAY_SIZE(packetTotals));
    channelIndex = 0;
    if (singleChannel) {
        isChannelReady = 0;
    } else {
        isChannelReady = 1;
    }
    isMQTTConnected = 0;
    isMQTTCreated = 0;
    connectToBroker(mqttBroker, nodeNum, mqttPort);

    packetMap = kh_init(pckM);
    lastSentMap = kh_init(lastM);

    runCommand(ifplugdCommand, wifiInterface); //Kill ifplugd on wlan0 since it interferes
    runCommand(wpaSupCommand, NULL);  //Kill wpa_supplicant since it interferes

    pcapHandle = initialisePcap("wlan0");

    if (pcapHandle == NULL) {
        cleanup(5);
    }

    //Start channel switcher thread
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&channelThread, &attr, channelSwitcher, NULL);
    pthread_create(&commsThread, &attr, sendToServer, NULL);
    isChannelThreadRunning  = 1;
    pthread_attr_destroy(&attr);

    printf("Finished initialisation.\n");

    pcap_loop(pcapHandle, -1, readPacket, NULL);

    //Should never reach here but just incase...
    cleanup(-1);
}

/*int runCommand(char *command, char **args) {
  pid_t pid = vfork();
  int retval = -1;
  if (pid) {  //parent
  int status;
  if (waitpid(pid, &status, 0) == -1) {
//error
printf("Error occurred waiting for child.");
}
if (WIFEXITED(status)) {
retval = WEXITSTATUS(status);
}
} else {    //child
if (execvp(command, args) == -1) {
exit(99);
}
}
return retval;
}*/

/* Runs a given command and a single argument.
 * Returns the exit value of the command run.
 */
int runCommand(const char *command, char *arg) {
    FILE *fp;
    int retval = -1;
    char buffer[32];
    if (arg != NULL) {
        snprintf(buffer, ARRAY_SIZE(buffer), command, arg);
        fp = popen(buffer, "r");
    } else {
        fp = popen(command, "r");
    }
    if (fp == NULL) {
        fprintf(stderr, "Failed to run command\n");
        return retval;
    }

    /*while (fgets(buffer, ARRAY_SIZE(buffer) - 1, fp) != NULL) {

      }*/

    retval = pclose(fp);
    return retval;
}

/* Cleanup function called before exit.
 */
void cleanup(int ret) {
    void *status;
    closePcap(pcapHandle);
    signalList(&packetList);
    if (isChannelThreadRunning) {
        pthread_cancel(channelThread);
        pthread_join(channelThread, &status);
        isChannelThreadRunning = 0;
    }
    pthread_join(commsThread, &status);
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
    destroyList(&packetList, LIST_DELETE_DATA);
    destroyStoredData();
    kh_destroy(lastM, lastSentMap);
    free(wifiInterface);
    exit(ret);
}

/* Clean up function for deleting and freeing all lists and map data.
 */
void destroyStoredData(void) {
    struct linkedList *list;
    for (khint64_t it = kh_begin(packetMap); it != kh_end(packetMap); it++) {
        if (kh_exist(packetMap, it)) {
            list = kh_value(packetMap, it);
            destroyList(list, LIST_DELETE_DATA);
            free(list);
        }
    }
    kh_destroy(pckM, packetMap);
}

/* Initialises the wifi interface and pcap handle.
 * Returns the newly created pcap handle for the given interface.
 */
pcap_t* initialisePcap(char *device) {
    pcap_t *pcapHandle;
    char errbuf[PCAP_ERRBUF_SIZE];
    int retval;
    struct timespec sleepTime;

    sleepTime.tv_sec = 0;
    sleepTime.tv_nsec = 500000000L;

    //Wait half a second then bring down wifi interface
    nanosleep(&sleepTime, NULL);
    runCommand(ifconfigDownCommand, wifiInterface);

    //Create pcap handle
    pcapHandle = pcap_create(device, errbuf);
    if (pcapHandle == NULL) {
        fprintf(stderr, "pcap_create: %s\n", errbuf);
        closePcap(pcapHandle);
        return NULL;
    }

    //Set pcap handle to RF monitor mode
    if (pcap_can_set_rfmon(pcapHandle) == 1) {
        pcap_set_rfmon(pcapHandle, 1);
    } else {
        fprintf(stderr, "Failed to enter monitor mode. Are you running as root?\n");
        closePcap(pcapHandle);
        return NULL;
    }

    //Sleep for another half second before bringing interface up again
    nanosleep(&sleepTime, NULL);
    runCommand(ifconfigUpCommand, wifiInterface);

    //Make pcap handle active (will start receiving packets)
    retval = pcap_activate(pcapHandle);
    if (retval != 0) {
        fprintf(stderr, "Failed to activate pcap handle: %d\n", retval);
        fprintf(stderr, "%s\n", pcap_geterr(pcapHandle));
        closePcap(pcapHandle);
        return NULL;
    }

    //printf("link type: %d\n", pcap_datalink(pcapHandle));
    isPcapOpen = 1;

    return pcapHandle;
}

/* Closes the open pcap handle.
 */
void closePcap(pcap_t *pcapHandle) {
    isPcapOpen = 0;
    if (pcapHandle != NULL) {
        pcap_breakloop(pcapHandle);
        pcap_close(pcapHandle);
    }
}

/* Callback function for pcap loop. Is called every time a packet is received.
 */
void readPacket(u_char *args, const struct pcap_pkthdr *header,
                const u_char *packet) {

    struct wisnPacket *wisnData;
    struct ieee80211_header *ieee80211Header;
    unsigned short channel;
    struct ieee80211_radiotap_iterator iterator;
    struct ieee80211_radiotap_header *radiotapHeader = (struct ieee80211_radiotap_header*)(packet);

    int retval = ieee80211_radiotap_iterator_init(&iterator, radiotapHeader,
            header->caplen, NULL);

    ieee80211Header = (struct ieee80211_header *)(packet + radiotapHeader->it_len);

    //Removes AP beacon spam
    if (get802Type(ieee80211Header) != IEEE80211_MANAGEMENT || get802Subtype(ieee80211Header) != 8) {
        int ret;
        unsigned char *addr;
        char buff[16];
        struct tm *tmInfo;
        time_t now = time(NULL);
        struct linkedList *list;
        struct linkedNode *node;
        struct linkedNode *nextNode;
        struct wisnPacket *nodePacket;
        khint64_t it;
        unsigned long long mac = 0;
        /*if (get802Type(ieee80211Header) == IEEE80211_CONTROL && (get802Subtype(ieee80211Header) == 12 ||
          get802Subtype(ieee80211Header) == 13)) {
          addr = ieee80211Header->address1;
          } else {*/
        addr = ieee80211Header->address2;
        //}

        memcpy(&mac, addr, ARRAY_SIZE(ieee80211Header->address2));
        wisnData = malloc(sizeof(*wisnData));
        wisnData->timestamp = (unsigned long long)now;
        memcpy(wisnData->mac, addr, ARRAY_SIZE(wisnData->mac));
        wisnData->nodeNum = nodeNum;
        wisnData->rssi = 0;
        channel = 0;

        //Check for
        //if (radiotapHeader->it_present & 0x810) {
        while (!retval) {
            retval = ieee80211_radiotap_iterator_next(&iterator);
            if (retval) {
                continue;
            }

            if (iterator.this_arg_index == IEEE80211_RADIOTAP_CHANNEL) {
                channel = *((unsigned short *)iterator.this_arg);
            } else if (iterator.this_arg_index == IEEE80211_RADIOTAP_DBM_ANTSIGNAL) {
                wisnData->rssi = 256 - *(iterator.this_arg);
                break;
            } else if (iterator.this_arg_index == IEEE80211_RADIOTAP_DB_ANTSIGNAL) {
                wisnData->rssi = *(iterator.this_arg);
                break;
            }
        }
        //}

        //Increment channel packet counter if running
        if (isChannelReady) {
            packetTotals[channelIndex]++;
        }

        it = kh_get(pckM, packetMap, mac);
        if (it != kh_end(packetMap)) { //An entry for this device already exists
            list = kh_value(packetMap, it);
            node = list->head;
            while (node != NULL) {
                nodePacket = node->data;
                //Remove readings older than x minutes and prune to AVGNUM size
                if (list->size > AVGNUM || now - nodePacket->timestamp > AVGTIMEOUT) {
                    nextNode = node->next;
                    removeNode(list, node, LIST_NO_LOCK, LIST_DELETE_DATA);
                    node = nextNode;
                } else {
                    break;  //Chronological order so no nodes past current will be older
                }
            }
        } else { //No entry exists
            list = malloc(sizeof(struct linkedList));
            initList(list);
            it = kh_put(pckM, packetMap, mac, &ret);
            kh_value(packetMap, it) = list;
        }
        addDataToTailList(list, clonePacket(wisnData));

        //Calculate average reading from list
        wisnData->rssi = 0;
        node = list->head;
        nodePacket = node->data;
        while (node != NULL) {
            wisnData->rssi += (double)nodePacket->rssi;
            node = node->next;
        }
        wisnData->rssi /= (double)list->size;

        memset(buff, 0, ARRAY_SIZE(buff));
        tmInfo = localtime(&now);
        strftime(buff, ARRAY_SIZE(buff), "%X", tmInfo);
        printf("time: %s, type: %u, subtype: %u, channel: %u %02X:%02X:%02X:%02X:%02X:%02X - %02X:%02X:%02X:%02X:%02X:%02X - %f dBm\n",
                buff, get802Type(ieee80211Header), get802Subtype(ieee80211Header), getChannel(channel),
                ieee80211Header->address1[0], ieee80211Header->address1[1], ieee80211Header->address1[2],
                ieee80211Header->address1[3], ieee80211Header->address1[4], ieee80211Header->address1[5],
                wisnData->mac[0], wisnData->mac[1], wisnData->mac[2], wisnData->mac[3], wisnData->mac[4],
                wisnData->mac[5], wisnData->rssi);

        it = kh_get(lastM, lastSentMap, mac);
        if (it != kh_end(lastSentMap)) { //An entry for this device already exists
            if (now - kh_value(lastSentMap, it) > 0) {
                kh_value(lastSentMap, it) = now;
                addDataToTailList(&packetList, wisnData);
            }
        } else { //No existing entry
            it = kh_put(lastM, lastSentMap, mac, &ret);
            kh_value(lastSentMap, it) = now;
            addDataToTailList(&packetList, wisnData);
        }
    }
}

/* Changes the wifi channel of the wifi interface using iwconfig.
 */
void changeChannel(char channel) {
    char command[32];
    int retval;
    printf("Changing channel to %d\n", channel);
    if (channel < 15 && channel > 0) {
        memset(command, 0, ARRAY_SIZE(command));
        snprintf(command, ARRAY_SIZE(command), iwconfigChannelCommand,
                 wifiInterface, channel);
        retval = runCommand((const char *)command, NULL);
        if (retval != 0) {
            fprintf(stderr, "Error changing channel %d\n", retval);
        }
    }
}

/* Converts the given frequency to the correct wifi channel.
 * Returns the wifi channel between 1 and 14; otherwise -1 for non-matching frequencies/
 */
char getChannel(short frequency) {
    if (frequency == 2484) {
        return 14;
    }

    if (frequency < 2484) {
        return (frequency - 2407) / 5;
    }

    return -1;
}

/* Thread responsible for switching wifi channels periodically.
 * Only returns when the pcap handle is closed.
 */
void *channelSwitcher(void *arg) {
    struct timespec sleepTime;
    struct timespec sleepTimeLeft;
    time_t channelTimes[NUMCHANNELS];
    volatile unsigned char currentChannel = 0;
    volatile unsigned char detectPhase = 1;

    if (singleChannel) {
        changeChannel(singleChannel);
        pthread_exit(NULL);
    }

    while (isPcapOpen) {
        if (detectPhase) {
            isChannelReady = 1;
            currentChannel++;
            channelIndex = currentChannel - 1;
            changeChannel(currentChannel);
            sleepTime.tv_sec = 21;
            sleepTime.tv_nsec = 500000000L;
            if (currentChannel == 14) {
                detectPhase = 0;
                currentChannel = 255;
            }
        } else {
            if (currentChannel == 255) {
                calculateChannelTimeSlices(channelTimes);
                currentChannel = 0;
            }
            currentChannel++;
            if (currentChannel > 14) {
                detectPhase = 1;
                currentChannel = 0;
                continue;
            }
            channelIndex = currentChannel - 1;
            if (channelTimes[channelIndex] > 0) {
                sleepTime.tv_sec = channelTimes[channelIndex];
                sleepTime.tv_nsec = 0;
                changeChannel(currentChannel);
            } else {
                continue;
            }
            if (currentChannel == 14) {
                detectPhase = 1;
                currentChannel = 0;
            }
        }

        while (nanosleep(&sleepTime, &sleepTimeLeft) != 0) {
            memcpy(&sleepTime, &sleepTimeLeft, sizeof(sleepTimeLeft));
        }
    }
    pthread_exit(NULL);
}

/* Calculates how much time to spend listening on each channel
 */
void calculateChannelTimeSlices(time_t *channelTime) {
    unsigned int totalPackets = 0;

    isChannelReady = 0;

    for (int i = 0; i < ARRAY_SIZE(packetTotals); i++) {
        totalPackets += packetTotals[i];
    }

    printf("Total packets: %d\n", totalPackets);
    for (int i = 0; i < NUMCHANNELS; i++) {
        channelTime[i] = 3600 * packetTotals[i] / totalPackets;
        printf("Channel %d received %d packets, allocating %d seconds.\n",
               i + 1, packetTotals[i], (int)channelTime[i]);
        packetTotals[i] = 0;
    }

    //isChannelReady = 1;
}

/* Compares the two given MAC addresses
 * Returns 0 if the two addresses don't match; otherwise non-zero.
 */
char compareMAC(const unsigned char *mac1, const unsigned char *mac2) {
    char match = 1;
    for (int i = 0; i < 6; i++) {
        if (*(mac1 + i) != *(mac2 + i)) {
            match = 0;
            break;
        }
    }
    return match;
}

/* Checks the given interface name with all interfaces
 * Returns 0 if no match is found; otherwise non-zero.
 */
char checkInterface(char *interface) {
    struct ifaddrs *addrs;
    struct ifaddrs *nextAddr;
    char match = 0;
    int retval = getifaddrs(&addrs);
    if (retval != 0) {
        fprintf(stderr, "Error reading network interfaces\n");
        exit(4);
    }

    //Iterate through all interfaces
    for (nextAddr = addrs; nextAddr != NULL; nextAddr = nextAddr->ifa_next) {
        if (strcmp(interface, nextAddr->ifa_name) == 0) {
            match = 1;
            break;
        }
    }
    freeifaddrs(addrs);
    return match;
}

/* Converts the data from a wisnPacket struct in a serialised form.
 * Returns a pointer to a buffer containing the serialised data.
 */
/*unsigned char *serialiseWisnPacket(struct wisnPacket *packet, unsigned int *size) {
  unsigned char *buffer;
  unsigned char *marker;

 *size = sizeof(packet->timestamp) + ARRAY_SIZE(packet->mac) +
 sizeof(packet->rssi) + sizeof(packet->nodeNum);

 buffer = malloc(*size);
 marker = buffer;

 packet->timestamp = htobe64(packet->timestamp);
 memcpy(marker, &(packet->timestamp), sizeof(packet->timestamp));
 marker += sizeof(packet->timestamp);

 memcpy(marker, packet->mac, ARRAY_SIZE(packet->mac));
 marker += ARRAY_SIZE(packet->mac);

 memcpy(marker, &(packet->rssi), sizeof(packet->rssi));
 marker += sizeof(packet->rssi);

 packet->nodeNum = htons(packet->nodeNum);
 memcpy(marker, &(packet->nodeNum), sizeof(packet->nodeNum));
 marker += sizeof(packet->nodeNum);

 return buffer;
 }*/

/* Signals anything waiting on the given list.
 */
void signalList(struct linkedList *list) {
    if (pthread_cond_signal((&list->cond))) {
        fprintf(stderr, "Error signalling condition variable.\n");
    }
}

/* Attempts to connect to the given MQTT broker.
 */
unsigned int connectToBroker(char *address, unsigned int nodeNum, int port) {
    char clientID[24];
    unsigned int res = 255;

    memset(clientID, 0, ARRAY_SIZE(clientID));
    snprintf(clientID, ARRAY_SIZE(clientID), "wisn%03u", nodeNum);

    mosquitto_lib_init();
    mosqConn = mosquitto_new(clientID, 1, NULL);
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
    packetList.doSignal = 1;

    mosquitto_reconnect_delay_set(mosqConn, RECONNECTDELAY, RECONNECTDELAY, 0);

    res = mosquitto_loop_start(mosqConn);
    if (res != MOSQ_ERR_SUCCESS) {
        fprintf(stderr, "Failed to start MQTT connection loop.\n");
        return res;
    }

    return res;
}

/* Thread for sending all received wifi packets to the server.
 */
void *sendToServer(void *arg) {
    char buffer[256];
    int ret;

    while (pthread_mutex_lock(&(packetList.mutex))) {
        fprintf(stderr, "Error acquiring list mutex.\n");
    }

    while (isPcapOpen) {
        while (packetList.head == NULL && isPcapOpen) {
            if (pthread_cond_wait(&(packetList.cond), &(packetList.mutex))) {
                fprintf(stderr, "Error waiting for condition variable signal\n");
            }
        }

        if (!isPcapOpen) {
            break;
        }

        JSONisePacket(packetList.head->data, buffer, ARRAY_SIZE(buffer));
        ret = mosquitto_publish(mosqConn, NULL, MQTTTopic, strlen(buffer), buffer, 0, 0);
        if (ret == MOSQ_ERR_INVAL) {
            fprintf(stderr, "Error sending message - Invalid parameters.\n");
        } else if (ret == MOSQ_ERR_NO_CONN) {
            fprintf(stderr, "Error sending message - No connection.\n");
        } else if (ret == MOSQ_ERR_PROTOCOL) {
            fprintf(stderr, "Error sending message - Protocol error.\n");
        } else if (ret == MOSQ_ERR_PAYLOAD_SIZE) {
            fprintf(stderr, "Error sending message - Payload too large.\n");
        } else {
            removeFromHeadList(&packetList, LIST_HAVE_LOCK, LIST_DELETE_DATA);
        }
    }

    if (pthread_mutex_unlock(&(packetList.mutex))) {
        fprintf(stderr, "Error releasing list mutex.\n");
    }

    pthread_exit(NULL);
}

/* Turns the given packet into a JSON structure.
 */
void JSONisePacket(struct wisnPacket *packet, char *buffer, int size) {
    memset(buffer, 0, size);

    snprintf(buffer, size,
            "{\"node\":%d,\"time\":%llu,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"rssi\":%f}",
            packet->nodeNum, packet->timestamp, packet->mac[0], packet->mac[1],
            packet->mac[2], packet->mac[3], packet->mac[4], packet->mac[5], packet->rssi);
}
