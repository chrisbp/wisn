CC = gcc
CFLAGS = -Wall -pedantic --std=gnu99 -lpcap -lmosquitto -lm -pthread -Os
CSVRFLAGS = -Wall -pedantic --std=gnu99 -lmosquitto -lgsl -lgslcblas -L/usr/local/lib -I/usr/local/include/libmongoc-1.0 -I/usr/local/include/libbson-1.0 -lmongoc-1.0 -lbson-1.0 -lm -pthread -Os

.PHONY: all clean

all : wisn wisn_server

cleanmake : clean all

wisn : radiotap.o ieee80211.o  linked_list.o wisn_packet.o wisn.c wisn.h
	$(CC) -c wisn.c $(CFLAGS)
	$(CC) -o wisn wisn.o radiotap.o ieee80211.o linked_list.o wisn_packet.o $(CFLAGS)

wisn_server : linked_list.o wisn_packet.o wisn_server.c wisn_server.h
	$(CC) -c wisn_server.c $(CSVRFLAGS)
	$(CC) -o wisn_server wisn_server.o linked_list.o wisn_packet.o $(CSVRFLAGS)

linked_list.o : linked_list.c linked_list.h
	$(CC) -c linked_list.c $(CFLAGS)

radiotap.o : radiotap.c radiotap.h radiotap_iter.h
	$(CC) -c radiotap.c $(CFLAGS)

ieee80211.o : ieee80211.c ieee80211.h
	$(CC) -c ieee80211.c $(CFLAGS)

wisn_packet.o : wisn_packet.c wisn_packet.h
	$(CC) -c wisn_packet.c $(CFLAGS)

clean :
	rm -f wisn wisn_server *.o
