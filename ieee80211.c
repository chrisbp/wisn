#include "ieee80211.h"

//Returns the 802.11 packet Type
unsigned char get802Type(struct ieee80211_header *header) {
        return (header->frameControl & 0x0C) >> 2;
}

//Returns the 802.11 packet Subtype
unsigned char get802Subtype(struct ieee80211_header *header) {
        return (header->frameControl & 0xF0) >> 4;
}

//Returns the 802.11 ToDS bit
unsigned char get802ToDS(struct ieee80211_header *header) {
        return (header->frameControl & 0x100) >> 8;
}

//Returns the 802.11 FromDS bit
unsigned char get802FromDS(struct ieee80211_header *header) {
        return (header->frameControl & 0x200) >> 9;
}

//Returns the 802.11 More Fragments bit
unsigned char get802MoreFrags(struct ieee80211_header *header) {
        return (header->frameControl & 0x400) >> 10;
}

//Returns the 802.11 Retry bit
unsigned char get802Retry(struct ieee80211_header *header) {
        return (header->frameControl & 0x800) >> 11;
}

//Returns the 802.11 Power Management bit
unsigned char get802PowerManagement(struct ieee80211_header *header) {
        return (header->frameControl & 0x1000) >> 12;
}

//Returns the 802.11 More Data bit
unsigned char get802MoreData(struct ieee80211_header *header) {
        return (header->frameControl & 0x2000) >> 13;
}

//Returns the 802.11 Protected Frame bit
unsigned char get802ProtectedFrame(struct ieee80211_header *header) {
        return (header->frameControl & 0x4000) >> 14;
}

//Returns the 802.11 Order bit
unsigned char get802Order(struct ieee80211_header *header) {
        return (header->frameControl & 0x8000) >> 15;
}
