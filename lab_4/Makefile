.PHONY: all clean


all: osi_server osi_client

osi_server: server.cpp
	g++ server.cpp -Wall -Wextra -o osi_server -pthread

osi_client: client.cpp
	g++ client.cpp -Wall -Wextra -o osi_client -pthread

clean:
	rm -rf *.o osi_server osi_client
