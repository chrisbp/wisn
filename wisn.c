#include "wisn.h"

KHASH_MAP_INIT_INT64(pckM, time_t)

const char *ifplugdCommand = "ifplugd -i %s -k";                //Command to stop ifplugd
const char *wpaSupCommand = "killall wpa_supplicant";           //Command to stop wpa_supplicant
const char *ifconfigDownCommand = "ifconfig %s down";           //Command to bring down a network interface
const char *ifconfigUpCommand = "ifconfig %s up";               //Command to bring up a network interface
const char *iwconfigChannelCommand = "iwconfig %s channel %d";  //Command to change wifi channel

unsigned int nodeNum;   //Number of this base node
char *wifiInterface;    //Name of wifi interface
const char *usage = "Usage: wisn node_num wifi_interface mqtt_address [mqtt_port]\n"; //Usage string

pcap_t *pcapHandle;             //Pcap handle for the wifi interface to listen on
volatile char isPcapOpen;       //Flag to indicate whether pcap handle is open or closed

pthread_t channelThread;        //Thread for channel switcher
pthread_t commsThread;          //Thread for communicating with MQTT broker
volatile char isChannelThreadRunning;   //Flag for if channels switcher is running

struct linkedList packetList;   //Linked list to queue up packets received over wifi
khash_t(pckM) *packetMap;       //Hashmap for recently received packets

#ifdef DISTANCE
int movAvg[32];
int movAvgSize = 0;
#endif

struct mosquitto *mosqConn;     //MQTT connection handle
int mqttPort;                   //MQTT broker port to connect on - default is 1883
volatile char isMQTTCreated;    //Flag for if MQTTClient has been initialised
volatile char isMQTTConnected;  //Flag for if connected to MQTT broker
char MQTTTopic[16];             //Topic for publishing messages

int main(int argc, char *argv[]) {
    unsigned char size;
    //Signal handler for kill/termination
    struct sigaction sa;
    pthread_attr_t attr;

    if (argc > 4) {
        nodeNum = strtoul(argv[1], NULL, 10);
        if (nodeNum < 1) {
            fprintf(stderr, "Invalid base number.\n");
            exit(2);
        }
        memset(MQTTTopic, 0, ARRAY_SIZE(MQTTTopic));
        snprintf(MQTTTopic, ARRAY_SIZE(MQTTTopic), "wisn/wisn%03u", nodeNum);

        if (checkInterface(argv[2]) == 0) {
            fprintf(stderr, "No network interface with name %s found.\n", argv[2]);
            exit(5);
        } else {
            size = sizeof(char) * (strlen(argv[2]) + 1);
            wifiInterface = (char *)calloc(1, size);
            memcpy(wifiInterface, argv[2], size);
        }
        if (argc > 4) {
            mqttPort = strtoul(argv[4], NULL, 10);
            if (mqttPort < 1 || mqttPort > 65535) {
                fprintf(stderr, "Invalid port\n");
                fprintf(stderr, usage);
                exit(2);
            }
        } else {
            mqttPort = MQTTPORT;
        }

        isMQTTConnected = 0;
        isMQTTCreated = 0;
        connectToBroker(argv[3], nodeNum, mqttPort);
    } else {
        fprintf(stderr, usage);
        exit(1);
    }

    //Setup signal handler
    sa.sa_handler = cleanup;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    isPcapOpen = 0;
    isChannelThreadRunning = 0;
    initList(&packetList);

    packetMap = kh_init(pckM);

    runCommand(ifplugdCommand, wifiInterface); //Kill ifplugd on wlan0 since it interferes
    runCommand(wpaSupCommand, NULL);  //Kill wpa_supplicant since it interferes

    pcapHandle = initialisePcap("wlan0");

    if (pcapHandle == NULL) {
        cleanup(6);
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
    destroyList(&packetList);
    kh_destroy(pckM, packetMap);
    free(wifiInterface);
    exit(ret);
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

#ifdef DISTANCE
double getDistance(double rssi, int type) {
    double res = 0;
    if (type == 1) {        //iperf linear
        res = (0.1994 * rssi) - 1.3107;
    } else if (type == 2) { //iperf power
        res = 0.0003 * pow(rssi, 2.9324);
    } else if (type == 3) { //ping linear
        res = (0.1551 * rssi) - 1.147;
    } else if (type == 4) { //ping power
        res = 0.0005 * pow(rssi, 2.5813);
    } else if (type == 5) { //mr3040 linear
        res = (0.1789 * rssi) - 1.4057;
    } else if (type == 6) { //mr3040 power
        res = 0.0003 * pow(rssi, 2.8125);
    } else if (type == 7) { //mr3040setDist linear
        res = (0.1782 * rssi) - 1.1795;
    } else if (type == 8) { //mr3040setDist power
        res = 0.0005 * pow(rssi, 2.7237);
    } else if (type == 9) { //multiphone linear
        res = (0.1637 * rssi) - 1.0448;
    } else if (type == 10) { //multiphone power
        res = 0.0002 * pow(rssi, 2.9179);
    }
    return res;
}
#endif

/* Callback function for pcap loop. Is called every time a packet is received.
 */
void readPacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
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
        unsigned long long mac = 0;
        /*if (get802Type(ieee80211Header) == IEEE80211_CONTROL && (get802Subtype(ieee80211Header) == 12 ||
          get802Subtype(ieee80211Header) == 13)) {
          addr = ieee80211Header->address1;
          } else {*/
        addr = ieee80211Header->address2;
        //}

        memcpy(&mac, addr, ARRAY_SIZE(ieee80211Header->address2));

        khint_t it = kh_get(pckM, packetMap, mac);
        if (it != kh_end(packetMap)) {
            if (now - kh_value(packetMap, it) < 2) {
                return;
            } else {
                kh_value(packetMap, it) = now;
            }
        } else {
            it = kh_put(pckM, packetMap, mac, &ret);
            kh_value(packetMap, it) = now;
        }

        wisnData = (struct wisnPacket *)malloc(sizeof(*wisnData));
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
        memset(buff, 0, ARRAY_SIZE(buff));
        tmInfo = localtime(&now);
        strftime(buff, ARRAY_SIZE(buff), "%X", tmInfo);
        printf("time: %s, type: %u, subtype: %u, channel: %u %02X:%02X:%02X:%02X:%02X:%02X - %02X:%02X:%02X:%02X:%02X:%02X - %d dBm\n",
                buff, get802Type(ieee80211Header), get802Subtype(ieee80211Header), getChannel(channel),
                ieee80211Header->address1[0], ieee80211Header->address1[1], ieee80211Header->address1[2],
                ieee80211Header->address1[3], ieee80211Header->address1[4], ieee80211Header->address1[5],
                wisnData->mac[0], wisnData->mac[1], wisnData->mac[2], wisnData->mac[3], wisnData->mac[4],
                wisnData->mac[5], wisnData->rssi);
        #ifdef DISTANCE
        double avg = 0;
        movAvg[movAvgSize] = wisnData->rssi;
        movAvgSize++;
        if (movAvgSize == 1) {
            avg = wisnData->rssi;
        } else {
            while (movAvgSize > 16) {
                for (int i = 1; i < movAvgSize; i++) {
                    movAvg[i - 1] = movAvg[i];
                }
                movAvg[movAvgSize - 1] = 0;
                movAvgSize--;
            }
            for (int i = 0; i < movAvgSize; i++) {
                avg += movAvg[i];
            }
            avg /= movAvgSize;
        }
        fprintf(stdout, "%f\n", avg);
        //        fprintf(stdout, "distance 1: %f m, 2: %f m\n", getDistance(avg, 1), getDistance(avg, 2));
        //        fprintf(stdout, "distance 3: %f m, 4: %f m\n", getDistance(avg, 3), getDistance(avg, 4));
        //        fprintf(stdout, "distance 5: %f m, 6: %f m\n", getDistance(avg, 5), getDistance(avg, 6));
        //        fprintf(stdout, "distance 7: %f m, 8: %f m\n", getDistance(avg, 7), getDistance(avg, 8));
        fprintf(stdout, "distance 6: %f m, distance 9: %f m, 10: %f m\n", getDistance(avg, 6), getDistance(avg, 9), getDistance(avg, 10));
        #endif

        addPacketToTailList(&packetList, wisnData);
    }
}

/* Changes the wifi channel of the wifi interface using iwconfig.
 */
void changeChannel(char channel) {
    char command[32];
    int retval;
    if (channel < 15 && channel > 0) {
        memset(command, 0, ARRAY_SIZE(command));
        snprintf(command, ARRAY_SIZE(command), iwconfigChannelCommand, wifiInterface, channel);
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
    char channelSkip = 1;
    char currentChannel = 1;
    struct timespec sleepTime;
    struct timespec sleepTimeLeft;

    sleepTime.tv_sec = 30;
    //sleepTime.tv_nsec = 500000000L;
    sleepTime.tv_nsec = 0;

    changeChannel(1);

    while (isPcapOpen) {
        changeChannel(currentChannel);
        currentChannel += channelSkip;
        if (currentChannel > 14 || currentChannel < 1) {
            currentChannel = 1;
        }
        while (nanosleep(&sleepTime, &sleepTimeLeft) != 0) {
            memcpy(&sleepTime, &sleepTimeLeft, sizeof(sleepTimeLeft));
        }
    }
    pthread_exit(NULL);
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

 buffer = (unsigned char *)malloc(*size);
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
            removeFromHeadList(&packetList, 1);
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
            "{\"node\":%d,\"x\":%d,\"y\":%d,\"time\":%llu,\"mac\":\"%02X%02X%02X%02X%02X%02X\",\"rssi\":%d}",
            packet->nodeNum, packet->x, packet->y, packet->timestamp, packet->mac[0], packet->mac[1],
            packet->mac[2], packet->mac[3], packet->mac[4], packet->mac[5], packet->rssi);
}
