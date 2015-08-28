CLIENT_LIBS = `pkg-config gstreamer-1.0 --libs`
SERVER_LIBS = `pkg-config gstreamer-pbutils-1.0 gstreamer-tag-1.0 gstreamer-video-1.0 gstreamer-audio-1.0 --libs`
CC = gcc
CLIENT_CFLAGS = -g -Wall `pkg-config --cflags gstreamer-1.0`
SERVER_CFLAGS = -g -Wall `pkg-config --cflags gstreamer-pbutils-1.0`

CLIENT_SOURCE = client.c
SERVER_SOURCE = tcp_server.c

.PHONY: default all clean

all: tcp_client tcp_server

tcp_client: $(CLIENT_SOURCE)
	$(CC) $(CLIENT_SOURCE) $(CLIENT_CFLAGS) $(CLIENT_LIBS) -o $@

tcp_server: $(SERVER_SOURCE)
	$(CC) $(SERVER_SOURCE) $(SERVER_CFLAGS) $(SERVER_LIBS) -o $@


clean:
	-rm -f *.o
	-rm -f tcp_client tcp_server
