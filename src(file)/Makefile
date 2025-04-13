CC = gcc
CFLAGS = -Wall -g
LIBS = -lwebsockets -lssl -lcrypto

all: server_ws client_ws client_tcp2ws server_tcpws client_ws2tcp client_rawtcp

server_ws: server_ws.c
	$(CC) $(CFLAGS) -o server_ws server_ws.c $(LIBS)

client_ws: client_ws.c
	$(CC) $(CFLAGS) -o client_ws client_ws.c $(LIBS)

client_tcp2ws: client_tcp2ws.c
	$(CC) $(CFLAGS) -o client_tcp2ws client_tcp2ws.c $(LIBS)

server_tcpws: server_tcpws.c
	$(CC) $(CFLAGS) -o server_tcpws server_tcpws.c $(LIBS)

client_ws2tcp: client_ws2tcp.c
	$(CC) $(CFLAGS) -o client_ws2tcp client_ws2tcp.c $(LIBS)

client_rawtcp: client_rawtcp.c
	$(CC) $(CFLAGS) -o client_rawtcp client_rawtcp.c $(LIBS)

clean:
	rm -f server_ws client_ws client_tcp2ws server_tcpws client_ws2tcp client_rawtcp
