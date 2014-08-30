CC = gcc
CFLAGS = -Wall -pedantic --std=gnu99 -lpcap -pthread -O1

.PHONY: all clean

all : wisn wisn_server

cleanmake : clean all

wisn : radiotap.o ieee80211.o  linked_list.o  wisn.c wisn.h
	$(CC) -c wisn.c $(CFLAGS)
	$(CC) -o wisn wisn.o radiotap.o ieee80211.o linked_list.o $(CFLAGS)

wisn_server : linked_list.o wisn_server.c wisn_server.h
	$(CC) -c wisn_server.c $(CFLAGS)
	$(CC) -o wisn_server wisn_server.o linked_list.o $(CFLAGS)

linked_list.o : linked_list.c linked_list.h
	$(CC) -c linked_list.c $(CFLAGS)

radiotap.o : radiotap.c radiotap.h radiotap_iter.h
	$(CC) -c radiotap.c $(CFLAGS)

ieee80211.o : ieee80211.c ieee80211.h
	$(CC) -c ieee80211.c $(CFLAGS)

clean :
	rm -f wisn wisn_server *.o

