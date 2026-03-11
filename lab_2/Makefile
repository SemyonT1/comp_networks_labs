.PHONY: all clean

all: tcp_client tcp_server

tcp_server: tcp_server.cpp
	g++ tcp_server.cpp -Wall -Wextra -o tcp_server
client: tcp_client.cpp
	g++ tcp_client.cpp -Wall -Wextra -o tcp_client

clean:
	rm -rf *.o tcp_client tcp_server