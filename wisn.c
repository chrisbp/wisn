#include "wisn.h"

KHASH_MAP_INIT_INT64(pckM, time_t)

const char *ifplugdCommand = "ifplugd -i %s -k";                //Command to stop ifplugd
const char *wpaSupCommand = "killall wpa_supplicant";           //Command to stop wpa_supplicant
const char *ifconfigDownCommand = "ifconfig %s down";           //Command to bring down a network interface
const char *ifconfigUpCommand = "ifconfig %s up";               //Command to bring up a network interface
const char *iwconfigChannelCommand = "iwconfig %s channel %d";  //Command to change wifi channel

unsigned int baseNum;   //Number of this base node
char *wifiInterface;    //Name of wifi interface
const char *usage = "Usage: wisn base_num wifi_interface server_address\n"; //Usage string

pcap_t *pcapHandle;             //Pcap handle for the wifi interface to listen on
volatile char isPcapOpen;       //Flag to indicate whether pcap handle is open or closed

pthread_t channelThread;        //Thread for channel switcher
pthread_t commsThread;          //Thread for sending data to server

struct linkedList packetList;   //Linked list to queue up packets received over wifi

khash_t(pckM) *packetMap;    //Hashmap for recently received packets

int sockFD; //File descriptor for connection to server

int main(int argc, char *argv[]) {
    //Signal handler for kill/termination
    struct sigaction sa;
    pthread_attr_t attr;

    if (argc == 4) {
        baseNum = strtoul(argv[1], NULL, 10);
        if (baseNum < 1) {
            fprintf(stderr, "Invalid base number.\n");
            exit(2);
        }
        if (checkInterface(argv[2]) == 0) {
            fprintf(stderr, "No network interface with name %s found.\n", argv[2]);
            exit(5);
        } else {
            unsigned char size = sizeof(char) * (strlen(argv[2]) + 1);
            wifiInterface = calloc(1, size);
            memcpy(wifiInterface, argv[2], size);
        }
        if (connectToServer(argv[3])) {
            close(sockFD);
            exit(6);
        }
    } else {
        fprintf(stderr, usage);
        exit(1);
    }

    //Setup signal handler
    sa.sa_handler = cleanup;
    sigaction(SIGINT, &sa, 0);
    sigaction(SIGTERM, &sa, 0);

    isPcapOpen = 0;
    initList(&packetList);

    packetMap = kh_init(pckM);

    runCommand(ifplugdCommand, wifiInterface); //Kill ifplugd on wlan0 since it interferes
    runCommand(wpaSupCommand, NULL);  //Kill wpa_supplicant since it interferes

    pcapHandle = initialisePcap("wlan0");

    if (pcapHandle == NULL) {
        exit(6);
    }

    //Start channel switcher thread
    //changeChannel(13);
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&commsThread, &attr, sendToServer, NULL);
    pthread_create(&channelThread, &attr, channelSwitcher, NULL);
    pthread_attr_destroy(&attr);

    printf("Finished initialisation.\n");

    pcap_loop(pcapHandle, -1, readPacket, NULL);

    //Should never reach here but just incase...
    cleanup();
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
void cleanup() {
    closePcap(pcapHandle);
    //try send any unsent messages here
    //then disconnect
    //then destroy MQTT client
    destroyList(&packetList, PACKET);
    kh_destroy(pckM, packetMap);
    free(wifiInterface);
    exit(0);
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

/* Closes the open pcap handle and waits on the channel switcher thread to exit.
 */
void closePcap(pcap_t *pcapHandle) {
    void *status;
    isPcapOpen = 0;
    if (pcapHandle != NULL) {
        pcap_breakloop(pcapHandle);
        pcap_close(pcapHandle);
    }
    signalList(&packetList);
    pthread_join(channelThread, &status);
    pthread_join(commsThread, &status);
    close(sockFD);
    pthread_exit(NULL);
}

/*void copyMAC(unsigned long long *dest, unsigned char *src) {
    *dest = 0;
    *dest |= *src << 40;
    *dest |= *(src + 1) << 32;
    *dest |= *(src + 2) << 24;
    *dest |= *(src + 3) << 16;
    *dest |= *(src + 4) << 8;
    *dest |= *(src + 5);
}*/

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
        ///
        char buff[16];
        struct tm *tmInfo;
        ///
        time_t now = time(NULL);
        unsigned long long mac = 0;
        if (get802Type(ieee80211Header) == IEEE80211_CONTROL && (get802Subtype(ieee80211Header) == 12 ||
            get802Subtype(ieee80211Header) == 13)) {
            addr = ieee80211Header->address1;
        } else {
            addr = ieee80211Header->address2;
        }

        memcpy(&mac, addr, ARRAY_SIZE(ieee80211Header->address2));
//        copyMAC(&mac, ieee80211Header->address2);

        khint_t it = kh_get(pckM, packetMap, mac);
        if (it != kh_end(packetMap)) {
            if (now - kh_value(packetMap, it) < 10) {
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
        wisnData->baseNum = baseNum;
        wisnData->rssi = 0;

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
    if (frequency == 2412) {
        return 1;
    } else if (frequency == 2417) {
        return 2;
    } else if (frequency == 2422) {
        return 3;
    } else if (frequency == 2427) {
        return 4;
    } else if (frequency == 2432) {
        return 5;
    } else if (frequency == 2437) {
        return 6;
    } else if (frequency == 2442) {
        return 7;
    } else if (frequency == 2447) {
        return 8;
    } else if (frequency == 2452) {
        return 9;
    } else if (frequency == 2457) {
        return 10;
    } else if (frequency == 2462) {
        return 11;
    } else if (frequency == 2467) {
        return 12;
    } else if (frequency == 2472) {
        return 13;
    } else if (frequency == 2484) {
        return 14;
    } else {
        return -1;
    }
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

/* Attempts to connect to the server at the given address.
 * Returns zero upon successful connection; otherwise non-zero.
 */
int connectToServer(char *serverAddress) {
    int status;
    struct addrinfo hints;
    struct addrinfo *results;
    struct addrinfo *r;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    status = getaddrinfo(serverAddress, PORT, &hints, &results);
    if (status) {
        fprintf(stderr, "Error getting server address info.\n");
        return status;
    }

    for (r = results; r != NULL; r = r->ai_next) {
        if (r->ai_family == AF_INET) {
            sockFD = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
            if (sockFD < 0) {
                fprintf(stderr, "Error creating socket\n");
                freeaddrinfo(results);
                return -1;
            }
            status = connect(sockFD, r->ai_addr, r->ai_addrlen);
            if (status) {
                close(sockFD);
                fprintf(stderr, "Error connecting to server\n");
                continue;
            } else {
                break;
            }
        }
    }
    freeaddrinfo(results);

    if (status) {
        fprintf(stderr, "Error initiating connection...exiting\n");
    }

    return status;
}

/* Thread for sending all received wifi packets to the server.
 */
void *sendToServer(void *arg) {
    unsigned char *buffer;
    unsigned int size;
    while (!pthread_mutex_lock(&(packetList.mutex))) {
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
        //send here
        buffer = serialiseWisnPacket(packetList.head->data.pdata, &size);
        if (send(sockFD, buffer, size, 0) < 0) {
            fprintf(stderr, "Error sending packet\n");
        }
        free(buffer);
        removeFromHeadList(&packetList, 1, PACKET);
    }

    if (pthread_mutex_unlock(&(packetList.mutex))) {
        fprintf(stderr, "Error releasing list mutex.\n");
    }

    pthread_exit(NULL);
}

/* Converts the data from a wisnPacket struct in a serialised form.
 * Returns a pointer to a buffer containing the serialised data.
 */
unsigned char *serialiseWisnPacket(struct wisnPacket *packet, unsigned int *size) {
    unsigned char *buffer;
    unsigned char *marker;

    *size = sizeof(packet->timestamp) + ARRAY_SIZE(packet->mac) +
        sizeof(packet->rssi) + sizeof(packet->baseNum);

    buffer = (unsigned char *)malloc(*size);
    marker = buffer;

    packet->timestamp = htobe64(packet->timestamp);
    memcpy(marker, &(packet->timestamp), sizeof(packet->timestamp));
    marker += sizeof(packet->timestamp);

    memcpy(marker, packet->mac, ARRAY_SIZE(packet->mac));
    marker += ARRAY_SIZE(packet->mac);

    memcpy(marker, &(packet->rssi), sizeof(packet->rssi));
    marker += sizeof(packet->rssi);

    packet->baseNum = htons(packet->baseNum);
    memcpy(marker, &(packet->baseNum), sizeof(packet->baseNum));
    marker += sizeof(packet->baseNum);

    return buffer;
}

/* Signals anything waiting on the given list.
 */
void signalList(struct linkedList *list) {
    if (pthread_cond_signal((&list->cond))) {
        fprintf(stderr, "Error signalling condition variable.\n");
    }
}

/*void connectToBroker(MQTTAsync *mqttClient, MQTTAsync_connectOptions *conn_opts,
        char *address, char * clientId) {
    MQTTAsync_create(mqttClient, address, clientId, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTAsync_setCallbacks(*mqttClient, NULL, connectionLost, NULL, NULL);
    conn_opts->cleansession = 1;
    conn_opts->onFailure = ;
    
    if (MQTTAsync_connect(*mqttClient, conn_opts) != MQTTASYNC_SUCCESS) {
        fprintf(stderr, "Failed to connect to broker: %s\n", address);
    }
}*/
