#ifndef IEEE80211_HEADER
#define IEEE80211_HEADER

struct ieee80211_header {
    unsigned short frameControl;
    unsigned short durationID;
    unsigned char address1[6];
    unsigned char address2[6];
    unsigned char address3[6];
    unsigned short sequenceControl;
    unsigned char address4[6];
} __attribute__((packed));

//802.11 packet types
#define IEEE80211_MANAGEMENT   0
#define IEEE80211_CONTROL      1
#define IEEE80211_DATA         2

unsigned char get802Type(struct ieee80211_header *header);
unsigned char get802Subtype(struct ieee80211_header *header);
unsigned char get802ToDS(struct ieee80211_header *header);
unsigned char get802FromDS(struct ieee80211_header *header);
unsigned char get802MoreFrags(struct ieee80211_header *header);
unsigned char get802Retry(struct ieee80211_header *header);
unsigned char get802PowerManagement(struct ieee80211_header *header);
unsigned char get802MoreData(struct ieee80211_header *header);
unsigned char get802ProtectedFrame(struct ieee80211_header *header);
unsigned char get802Order(struct ieee80211_header *header);
#endif
